"""专家接口协议角色反转、LINK/RELEASE、探测校验及回调时钟。"""

import threading
import time

import pytest

import dlt698 as d
from dlt698 import _native as n

from ..service.test_expert import Peers


@pytest.mark.parametrize(
    "profile", [d.ConnectionProfile.remote_public, d.ConnectionProfile.local_preset]
)
def test_protocol_server_can_dial_and_client_can_listen(profile):
    registry = n.ObjectRegistry()
    obj = n.MemoryObject()
    obj.set(2, d.Data.uint16(5000))
    n.register_standard_object(registry, 0x200F, b"\x02", obj)
    client = n.Engine()
    server = n.Engine(objects=registry)
    traffic = []

    def wait(engine, token):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            for current in [client, server]:
                for event in current.poll():
                    if event.traffic is not None:
                        traffic.append(event.traffic)
                    if current is engine and event.token == token:
                        assert event.error is None
                        return event
        raise AssertionError("native role-inverted operation did not complete")

    try:
        port = client.listen("127.0.0.1", 0, profile, role=n.Role.client)
        wait(server, server.connect_tcp("127.0.0.1", port, profile, role=n.Role.server))
        # 拨号完成不等于对端已接受；公共连接还须等待 LINK，预设连接也须等待 start。
        expected = (
            n.SessionState.associated
            if profile == d.ConnectionProfile.local_preset
            else n.SessionState.preconnected
        )
        deadline = time.monotonic() + 3
        while True:
            assert time.monotonic() < deadline
            server.poll()
            client.poll()
            try:
                if client.session_at().state == expected:
                    break
            except IndexError:
                pass
        if profile != d.ConnectionProfile.local_preset:
            assert wait(client, client.connect()).message.result == 0
        result = wait(client, client.get([d.Oad(oi=0x200F, attribute=2)])).message
        assert result.attributes[0].result.as_uint16() == 5000
        assert wait(server, server.link(n.LinkRequestType.heartbeat)).message.result == 0
        assert wait(client, client.release()).message is None
        assert {event.kind for event in traffic} == {"send", "receive"}
        assert traffic[0].bytes and traffic[0].timestamp > 0
    finally:
        client.close()
        server.close()
    assert traffic[0].bytes


def test_probe_preserves_data_dar_validation_failure_duplicates_and_native_callback():
    peers = Peers(public=True)
    try:
        # 已知频率点故意提供错误 Data 类型；保留原始值，同时携带标准校验诊断。
        frequency = d.Oad(oi=0x200F, attribute=2)
        bad = n.MemoryObject()
        bad.set(2, d.Data.uint32(5000))
        peers.registry.register_object(
            n.ObjectSchema(
                oi=0x200F, attributes=[n.AttributeSchema(number=2, type=d.DataType.uint32)]
            ),
            bad,
        )
        custom, missing = d.Oad(oi=0x4500, attribute=2), d.Oad(oi=0xF001, attribute=2)
        points = peers.wait(
            peers.client.probe_points(n.Capabilities(), [custom, missing, frequency, custom])
        ).points
        assert [item.attribute for item in points] == [custom, missing, frequency, custom]
        assert points[0].outcome.as_uint16() == 7
        assert points[1].outcome == 4
        assert points[2].outcome.as_uint32() == 5000
        assert points[2].schema_checked and points[2].validation_error is not None
        results = []
        n.async_probe_points(
            peers.client.session_at(), n.Capabilities(), [custom], n.ProbeOptions(), results.append
        )
        deadline = time.monotonic() + 3
        while not results:
            assert time.monotonic() < deadline
            peers.server.poll()
            peers.client.poll()
        assert results[0][0].outcome.as_uint16() == 7
        peers.client.session_at().request_close()
        peers.client.poll()
        points = peers.wait(peers.client.probe_points(n.Capabilities(), [custom, custom])).points
        assert all(point.outcome.code == d.ErrorCode.closed for point in points)
    finally:
        peers.close()


@pytest.mark.parametrize("failure_stage", ["start", "request"])
def test_clock_exception_closes_native_session_without_late_python_callbacks(failure_stage):
    calls = []
    fail_clock = failure_stage == "start"

    def failing_clock():
        calls.append(threading.get_ident())
        if fail_clock:
            raise ValueError("clock failed")
        return n.DateTime(value=[7, 234, 10, 18, 7, 0, 0, 0, 0, 0])

    peers = Peers(
        client_options=n.SessionOptions(
            calendar_clock=failing_clock,
            request_time_tag=n.Ti(unit=0, interval=60) if failure_stage == "request" else None,
        )
    )
    try:
        if failure_stage == "request":
            # 预设关联启动也会读取日历；等启动完成后再让请求时间标签触发异常。
            deadline = time.monotonic() + 3
            while not calls:
                assert time.monotonic() < deadline, "session clock was not initialized"
                peers.client.poll()
            assert peers.client.session_at().state == n.SessionState.associated
            assert calls == [threading.get_ident()]
            calls.clear()
            fail_clock = True

        token = peers.client.release()
        completions = []
        deadline = time.monotonic() + 3
        # poll() 仅推进给定时间预算，不保证单次调用就执行到 RELEASE 完成回调。
        while not completions:
            assert time.monotonic() < deadline, "RELEASE did not complete after clock failure"
            completions.extend(event for event in peers.client.poll() if event.token == token)
        assert len(completions) == 1
        assert completions[0].error is not None
        assert completions[0].error.code == (
            d.ErrorCode.closed if failure_stage == "start" else d.ErrorCode.invalid_value
        )
        assert peers.client.session_at().state == n.SessionState.closed
        assert calls == [threading.get_ident()]
    finally:
        peers.close()
    # close() 排空原生回调后仍只能调用一次失败时钟，不能在清理阶段重入 Python。
    assert calls == [threading.get_ident()]


def test_managed_thread_interfaces_reject_python_protocol_and_serial_hooks():
    options = n.SessionOptions(calendar_clock=lambda: n.DateTime())
    with pytest.raises(ValueError, match="caller-driven"):
        d.Client(n.ClientOptions(protocol=options))
    with pytest.raises(ValueError, match="caller-driven"):
        d.Server(options=n.ServerOptions(protocol=options))
    with d.Client() as client:
        with pytest.raises(ValueError, match="caller-driven"):
            client.open_serial_configured(
                "COM9999",
                n.SerialOptions(),
                n.SerialLinkOptions(async_drain=lambda done: done(None)),
            )
