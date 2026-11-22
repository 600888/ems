"""Exercise the real influxdb-cxx transport against a local v1-compatible HTTP fixture."""
import http.server
import datetime
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.parse


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.rows = {}
        self.errors = []
        self.alarm_batches = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, value, status=200):
        body = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def parameters(self, endpoint):
        url = urllib.parse.urlsplit(self.path)
        assert url.path == endpoint, url.path
        assert self.headers["Authorization"] == "Token test-secret"
        return {key: values[0] for key, values in urllib.parse.parse_qs(url.query).items()}

    def do_POST(self):
        try:
            params = self.parameters("/write")
            assert params["precision"] == "u"
            body = self.rfile.read(int(self.headers["Content-Length"])).decode()
            db = params["db"]
            if db == "slow":
                time.sleep(0.3)
            lines = body.splitlines()
            partial_alarm = False
            if db == "alarm_partial":
                assert len(lines) == 2
                assert lines[0].startswith("alarm_event,") and lines[1].startswith("alarm_state,")
                partial_alarm = not self.server.alarm_batches
                self.server.alarm_batches.append(body)
                if partial_alarm:
                    lines = lines[:1]
            if db in ("partial", "disconnect") and 'event_id=6661696c' in body:
                if db == "partial":
                    self.reply({"error": "partial write test-secret"}, 400)
                else:
                    self.connection.shutdown(socket.SHUT_RDWR)
                    self.connection.close()
                return
            if db in ("partial", "disconnect"):
                assert len(lines) == 1, "failed batch leaked into next request"
            for line in lines:
                identity, rest = line.split(" ", 1)
                fields, timestamp = rest.rsplit(" ", 1)
                measurement, *tags = identity.split(",")
                tags = dict(tag.split("=", 1) for tag in tags)
                parsed = {}
                parts = re.findall(r'([^,=]+)=("(?:\\.|[^"\\])*"|[^,]+)(?:,|$)', fields)
                for key, value in parts:
                    if value.startswith('"'):
                        parsed[key] = json.loads(value)
                    elif value.endswith("i"):
                        parsed[key] = int(value[:-1])
                    else:
                        parsed[key] = json.loads(value)
                if db == "alarm_partial":
                    payload = json.loads(parsed["payload"])
                    for name in ("alarm_id", "device_id", "station_id"):
                        assert tags[name] == payload[name].encode().hex()
                    if measurement == "alarm_event":
                        assert int(timestamp) == payload["event_time_us"]
                        assert tags["event_id"] == payload["event_id"].encode().hex()
                        kind = payload["kind"]
                        assert (parsed["kind"], parsed["stream_seq"]) == (
                            (0, 1) if kind == "Occurred" else (1, 2)
                        )
                    else:
                        assert parsed["occurred_at_us"] == payload["occurred_at_us"]
                        assert parsed["severity"] == "Critical"
                        assert parsed["acknowledged_at_us"] == -1
                        assert parsed["cleared_at_us"] == (
                            -1 if payload["cleared_at_us"] is None else payload["cleared_at_us"]
                        )
                        assert payload["name"] == "过温告警"
                else:
                    record = json.loads(parsed["record"])
                    assert int(timestamp) == record["source_time_us"] == parsed["source_time_us"]
                    assert tags["event_id"] == record["event_id"].encode().hex()
                    if record["value_type"] == 1 and record["value"] is not None:
                        assert parsed["sample_value"] == record["value"]
                key = (db, measurement, tuple(sorted(tags.items())), timestamp)
                self.server.rows[key] = (tags, parsed)
            if partial_alarm:
                self.reply({"error": "partial write"}, 400)
            else:
                self.reply({}, 204)
        except Exception as error:
            self.server.errors.append(str(error))
            self.reply({"error": "fixture rejected request"}, 500)

    def do_GET(self):
        try:
            params = self.parameters("/query")
            query, db = params["q"], params["db"]
            if "now()" in query:
                self.reply({"results": [{"statement_id": 0}]})
                return
            if db == "malformed":
                self.reply({"bad": "response"})
                return
            if db == "query_error":
                self.reply({"results": [{"error": "test-secret"}]})
                return
            measurement = re.search(r'FROM "(\w+)"', query)[1]
            tag_filters = re.findall(r'"(\w+)" = \'([^\']*)\'', query)
            bounds = re.findall(r'time (>=|>|<|=) (-?\d+)u', query)
            limit = int(re.search(r'LIMIT (\d+)', query)[1])
            if measurement in ("alarm_event", "alarm_state"):
                self.alarm_query(db, measurement, query, tag_filters, limit)
                return
            assert "ORDER BY time ASC" in query
            rows = []
            for (stored_db, name, _, timestamp), (tags, fields) in list(self.server.rows.items()):
                if stored_db != db or name != measurement:
                    continue
                if any(tags.get(key) != value for key, value in tag_filters):
                    continue
                stamp = int(timestamp)
                predicates = {">=": lambda a, b: a >= b, ">": lambda a, b: a > b,
                              "<": lambda a, b: a < b, "=": lambda a, b: a == b}
                if not all(predicates[op](stamp, int(value)) for op, value in bounds):
                    continue
                rows.append((stamp, tags, fields))
            # Deliberately put equal-time events in descending ID order.
            rows.sort(key=lambda row: row[1]["event_id"], reverse=True)
            rows.sort(key=lambda row: row[0])
            columns = ["time", "record", "source_time_us", "event_id", "point_id",
                       "series_key", "scope_kind", "scope_id"]
            values = [[(datetime.datetime(1970, 1, 1, tzinfo=datetime.timezone.utc) +
                        datetime.timedelta(microseconds=stamp)).isoformat().replace("+00:00", "Z"),
                       fields["record"], stamp,
                       tags["event_id"], tags["point_id"], tags["series_key"],
                       tags["scope_kind"], tags["scope_id"]]
                      for stamp, tags, fields in rows[:limit]]
            if db == "wrong_identity" and values:
                values[0][3] = "wrong"
            if db == "wrong_time" and values:
                values[0][0] = "2020-01-01T00:00:00Z"
            self.reply({"results": [{"series": [{"name": measurement, "columns": columns,
                                                    "values": values}]}]})
        except Exception as error:
            self.server.errors.append(str(error))
            self.reply({"error": "fixture rejected request"}, 500)

    def alarm_query(self, db, measurement, query, tag_filters, limit):
        upper = int(re.search(r'"stream_seq" <= (\d+)', query)[1])
        lower_time = re.search(r'"occurred_at_us" >= (\d+)', query)
        upper_time = re.search(r'"occurred_at_us" <= (\d+)', query)
        values = []
        for (stored_db, name, _, _), (tags, fields) in list(self.server.rows.items()):
            if stored_db != db or name != measurement or fields["stream_seq"] > upper:
                continue
            if any(tags.get(key) != value for key, value in tag_filters):
                continue
            if lower_time and not int(lower_time[1]) <= fields["occurred_at_us"] <= int(upper_time[1]):
                continue
            values.append([fields["stream_seq"], fields["payload"], tags["alarm_id"], tags["device_id"]])
        columns = ["stream_seq", "payload", "alarm_id", "device_id"]
        self.reply({"results": [{"series": [{"name": measurement, "columns": columns,
                                               "values": values[:limit]}]}]})


def main():
    with Server() as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        env = os.environ.copy()
        env["EMS_INFLUXDB_TEST_URL"] = f"http://127.0.0.1:{server.server_port}"
        env["NO_PROXY"] = "127.0.0.1"
        result = subprocess.run(sys.argv[1:], env=env, timeout=45)
        server.shutdown()
        if server.errors:
            raise AssertionError(server.errors)
        if server.alarm_batches and result.returncode == 0:
            first, retry, cleared, replay = server.alarm_batches
            assert first == retry == replay and cleared != first
            assert sum(key[0] == "alarm_partial" for key in server.rows) == 4
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
