"""低层服务扩展的拥有型参数、确认决策及一次性完成。"""

import gc
import hashlib

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.service import ProxyProvider, attach_advanced_services, attach_services


@pytest.fixture
def raw_pair():
    executor = n.ManualExecutor()
    a, b = n.MemoryChannel.pair(executor)
    client = n.SessionHandle(a, executor, n.SessionOptions(preset_association=True))
    server = n.SessionHandle(
        b, executor, n.SessionOptions(role=n.Role.server, preset_association=True)
    )
    client.start()
    server.start()
    executor.run_ready()
    try:
        yield executor, client, server
    finally:
        client.request_close()
        server.request_close()
        executor.run_ready()


def test_raw_request_handlers_own_inputs_and_can_be_removed(raw_pair):
    executor, client, server = raw_pair
    retained = []
    checks = []
    attribute = d.Oad(oi=0xF001, attribute=2)

    def read(request):
        retained.append(request)
        checks.append(server.in_executor_thread())
        return n.GetResponse(
            attributes=[n.AttributeResult(attribute=attribute, result=d.Data.uint16(42))]
        )

    def write(request):
        retained.append(request)
        return n.SetResponse(attributes=[n.SetResult(attribute=attribute, dar=0)])

    def action(request):
        retained.append(request)
        return n.ActionResponse(methods=[n.ActionResult(method=request.methods[0].method, dar=0)])

    def record(request):
        retained.append(request)
        return n.GetRecordResponse(
            records=[n.RecordResult(attribute=request.records[0].attribute, result=4)]
        )

    server.set_request_handler(read)
    server.set_set_handler(write)
    server.set_action_handler(action)
    server.set_record_handler(record)
    executor.run_ready()
    results = []
    client.async_get([attribute], False, results.append)
    executor.run_ready()
    assert results.pop().attributes[0].result.as_uint16() == 42
    client.async_set(
        [n.SetAttribute(attribute=attribute, value=d.Data.uint16(7))], False, results.append
    )
    executor.run_ready()
    assert results.pop().attributes[0].dar == 0
    client.async_action([n.ActionMethod(method=d.Omd(oi=0xF001, method=1))], False, results.append)
    executor.run_ready()
    assert results.pop().methods[0].dar == 0
    client.async_get_record([n.GetRecord(attribute=attribute)], False, results.append)
    executor.run_ready()
    assert results.pop().records[0].result == 4
    gc.collect()
    assert retained[0].attributes[0].oi == 0xF001
    assert retained[1].attributes[0].value.as_uint16() == 7
    assert retained[2].methods[0].method.oi == 0xF001
    assert retained[3].records[0].attribute.oi == 0xF001
    assert checks == [True] and not server.in_executor_thread()
    for name in ("request", "set", "action", "record"):
        getattr(server, f"set_{name}_handler")(None)
    executor.run_ready()
    client.async_get([attribute], False, results.append)
    executor.run_ready()
    assert results.pop().attributes[0].result == 4


def test_raw_advanced_completion_cancellation_and_late_result(raw_pair):
    executor, client, server = raw_pair
    pending = []
    cancelled = []

    def backend(request, done):
        pending.append((request, done))
        return lambda: cancelled.append(request.attribute.oi)

    server.set_advanced_handler(backend)
    executor.run_ready()
    results = []
    client.async_exchange(n.GetMd5Request(attribute=d.Oad(oi=0xF001, attribute=2)), results.append)
    executor.run_ready()
    request, done = pending[0]
    response = n.GetMd5Response(attribute=request.attribute, result=[ord("x")] * 16)
    done(response)
    executor.run_ready()
    assert bytes(results[0].result) == b"x" * 16
    with pytest.raises(ValueError, match="already delivered"):
        done(response)
    client.async_exchange(n.GetMd5Request(attribute=request.attribute), results.append)
    executor.run_ready()
    server.request_close()
    executor.run_ready()
    assert cancelled == [0xF001]
    pending[1][1](response)
    executor.run_ready()
    assert len(results) == 2 and isinstance(results[1], n.Error)


def test_raw_report_business_decides_ack_and_observes_follow_acd(raw_pair):
    executor, client, server = raw_pair
    reports, results, follows, access, diagnostics = [], [], [], [], []
    accept = [False]
    attribute = d.Oad(oi=0xF001, attribute=2)

    def report(message):
        reports.append(message)
        return accept[0]

    client.set_report_handler(report)
    client.set_follow_handler(follows.append)
    client.set_acd_handler(lambda: access.append(1))
    client.set_diagnostic_handler(diagnostics.append)
    server.set_access_demand(True)
    server.set_request_handler(
        lambda request: n.GetResponse(
            attributes=[n.AttributeResult(attribute=attribute, result=d.Data.uint16(7))],
            follow_report=[n.AttributeResult(attribute=attribute, result=d.Data.uint16(8))],
        )
    )
    executor.run_ready()
    client.async_get([attribute], False, results.append)
    executor.run_ready()
    assert follows and access
    results.clear()
    server.async_exchange(
        n.ReportNotification(
            payload=[n.AttributeResult(attribute=attribute, result=d.Data.uint16(9))]
        ),
        results.append,
    )
    executor.run_ready()
    assert len(reports) == 1 and not results
    accept[0] = True
    executor.advance(5)
    assert len(reports) == 2 and len(results) == 1
    assert isinstance(results[0], n.ReportResponse)
    assert reports[0].payload[0].result.as_uint16() == 9
    for name in ("report", "follow", "acd", "diagnostic", "advanced"):
        getattr(client, f"set_{name}_handler")(None)
    executor.run_ready()


def test_raw_install_advanced_services_and_python_proxy_own_arguments(raw_pair):
    executor, client, server = raw_pair
    registry = n.ObjectRegistry()
    obj = n.MemoryObject()
    attribute = d.Oad(oi=0xF001, attribute=2)
    obj.set(2, d.Data.uint16(7))
    registry.register_object(
        n.ObjectSchema(oi=0xF001, attributes=[n.AttributeSchema(number=2, type=d.DataType.uint16)]),
        obj,
    )
    retained = []

    class Proxy(ProxyProvider):
        def async_request(self, address, request, done):
            retained.extend([address, request, done])
            done(
                n.GetResponse(
                    list=True,
                    attributes=[
                        n.AttributeResult(attribute=request.attributes[0], result=d.Data.uint16(99))
                    ],
                )
            )

    proxy = Proxy()
    attach_services(server, registry)
    attach_advanced_services(server, registry, executor, n.AdvancedServiceOptions(proxy=proxy))
    del proxy
    gc.collect()
    executor.run_ready()
    results = []
    client.async_exchange(n.GetMd5Request(attribute=attribute), results.append)
    executor.run_ready()
    assert bytes(results.pop().result) == hashlib.md5(d.encode_data(d.Data.uint16(7))).digest()
    target = n.ProxyGetTarget(server=n.Tsa(value=b"\x00\x01"), items=[attribute])
    client.async_exchange(n.ProxyRequest(payload=[target]), results.append)
    executor.run_ready()
    assert results[0].payload[0].items[0].result.as_uint16() == 99
    assert retained[0].value == b"\x00\x01" and retained[1].attributes[0].oi == 0xF001


def test_proxy_failure_preserves_error_and_ignores_late_done():
    retained, results = [], []

    class Proxy(ProxyProvider):
        def async_request(self, address, request, done):
            retained.append(done)
            raise d.Dlt698Error(d.ErrorCode.remote_error, 3, "proxy failed", 8)

    proxy = Proxy()
    n.ProxyProvider.async_request(proxy, n.Tsa(value=b"\x00\x01"), n.GetRequest(), results.append)
    assert len(results) == 1 and results[0].code == d.ErrorCode.remote_error
    assert results[0].offset == 3 and results[0].remote_code == 8
    retained[0](n.GetResponse())
    assert len(results) == 1
    with pytest.raises(ValueError, match="already delivered"):
        retained[0](n.GetResponse())
