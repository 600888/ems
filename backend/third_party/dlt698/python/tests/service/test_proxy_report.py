"""原生 PROXY 路由与 REPORT 确认，独立 TCP 目标持续由同一线程驱动。"""

import time

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import AdvancedServiceOptions, Engine, ObjectRegistry, ProxyRouter

from ..service.test_expert import Peers


def test_proxy_get_routes_to_target_session():
    target = Peers()
    router = ProxyRouter()
    tsa = n.Tsa(value=b"\x00\x01")
    router.bind(tsa, target.client.session_at())
    middle = Engine(objects=ObjectRegistry(), advanced=AdvancedServiceOptions(proxy=router))
    outer = Engine()

    def wait(token):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            target.server.poll()
            target.client.poll()
            middle.poll()
            for event in outer.poll():
                if event.token == token:
                    assert event.error is None
                    return event
        raise AssertionError("proxy did not complete")

    try:
        port = middle.listen("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        wait(outer.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_preset))
        request = n.ProxyRequest(
            payload=[
                n.ProxyGetTarget(
                    server=tsa, timeout_seconds=1, items=[d.Oad(oi=0x4500, attribute=2)]
                )
            ]
        )
        response = wait(outer.exchange(request)).message
        assert response.payload[0].items[0].result.as_uint16() == 7
        assert n.advanced_matches(request, response)
    finally:
        outer.close()
        middle.close()
        target.close()


def test_report_notification_acknowledged_after_owning_snapshot():
    pair = Peers()
    received = []
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            pair.server.poll()
            pair.client.poll()
            try:
                if pair.server.session_at().state == n.SessionState.associated:
                    break
            except IndexError:
                pass
        token = pair.server.exchange(
            n.ReportNotification(
                payload=[
                    n.AttributeResult(
                        attribute=d.Oad(oi=0x4500, attribute=2), result=d.Data.uint16(8)
                    )
                ]
            )
        )
        deadline = time.monotonic() + 5
        complete = None
        while time.monotonic() < deadline and complete is None:
            received.extend(event for event in pair.client.poll() if event.kind == "report")
            for event in pair.server.poll():
                if event.token == token:
                    complete = event
        assert complete is not None and complete.error is None
        assert isinstance(complete.message, n.ReportResponse)
        assert received[0].message.payload[0].result.as_uint16() == 8
    finally:
        pair.close()
