"""认证和 SECURITY 后端委托测试；固定替身不具备密码学安全性。"""

import gc
import sys
import threading
import time

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import SecurityBackend
from dlt698.session import validate_session_options

from ..service.test_expert import Peers


class MockBackend(SecurityBackend):
    def __init__(self):
        super().__init__()
        self.opens = 0
        self.protects = 0
        self.resets = 0

    def begin_connect(self):
        return n.SymmetrySecurity(ciphertext=b"a", signature=b"b")

    def accept_connect(self, request):
        assert isinstance(request.mechanism, n.SymmetrySecurity)
        return n.AuthenticationResult(
            result=0, security=n.SecurityData(random=b"c", signature=b"d")
        )

    def verify_connect(self, request, response):
        assert response.security.signature == b"d"

    def protect_request(self, application):
        self.protects += 1
        return n.SecurityRequest(application=application, verification=n.Rn(value=b"c"))

    def open_request(self, message):
        self.opens += 1
        assert message.verification.value == b"c"
        return message.application

    def protect_response(self, application):
        self.protects += 1
        return n.SecurityResponse(application=application, verification=n.Mac(value=b"b"))

    def open_response(self, message):
        self.opens += 1
        assert message.verification.value == b"b"
        return message.application

    def reset(self):
        self.resets += 1


def test_authentication_and_protected_application_delegation():
    client_backend = MockBackend()
    server_backend = MockBackend()
    pair = Peers(
        public=True,
        client_options=d.SessionOptions(security_backend=client_backend),
        server_options=d.SessionOptions(security_backend=server_backend),
    )
    try:
        result = pair.wait(pair.client.get([d.Oad(oi=0x4500, attribute=2)])).message
        assert result.attributes[0].result.as_uint16() == 7
        assert client_backend.protects == client_backend.opens == 1
        assert server_backend.protects == server_backend.opens == 1
    finally:
        pair.close()
    assert client_backend.resets > 0
    assert server_backend.resets > 0


def test_security_backend_requires_all_methods_at_construction():
    with pytest.raises(TypeError, match="begin_connect.*reset"):
        SecurityBackend()

    class MissingReset(MockBackend):
        reset = SecurityBackend.reset

    with pytest.raises(TypeError, match="must implement: reset"):
        MissingReset()


def test_security_factory_is_per_session_and_reuse_cannot_reset_other_session():
    executor = n.ManualExecutor()
    a, b = n.MemoryChannel.pair(executor)
    backends = []
    owners = []

    def factory():
        owners.append(threading.get_ident())
        backend = MockBackend()
        backends.append(backend)
        return backend

    options = d.SessionOptions(security_backend_factory=factory)
    validate_session_options(options)
    assert backends == []
    first = n.SessionHandle(a, executor, options)
    second = n.SessionHandle(b, executor, options)
    assert len(backends) == 2 and backends[0] is not backends[1]
    assert owners == [threading.get_ident()] * 2
    c, other = n.MemoryChannel.pair(executor)
    with pytest.raises(ValueError, match="already in use"):
        n.SessionHandle(c, executor, d.SessionOptions(security_backend=backends[1]))
    with pytest.raises(ValueError, match="already in use"):
        n.SessionHandle(c, executor, d.SessionOptions(security_backend_factory=lambda: backends[1]))
    assert backends[1].resets == 0
    del first
    gc.collect()
    assert backends[0].resets == 1 and backends[1].resets == 0
    del second
    gc.collect()
    assert backends[1].resets == 1
    third = n.SessionHandle(c, executor, d.SessionOptions(security_backend=backends[1]))
    del third
    executor.run_ready()
    other.close()


def test_security_factory_validation_and_managed_callback_rejection():
    executor = n.ManualExecutor()
    a, b = n.MemoryChannel.pair(executor)
    calls = []

    def factory():
        calls.append(1)
        return MockBackend()

    options = d.SessionOptions(security_backend=MockBackend(), security_backend_factory=factory)
    with pytest.raises(ValueError, match="session options"):
        n.SessionHandle(a, executor, options)
    assert calls == []
    with pytest.raises(ValueError, match="factory returned null"):
        n.SessionHandle(a, executor, d.SessionOptions(security_backend_factory=lambda: None))
    with pytest.raises(ValueError, match="session options"):
        n.SessionHandle(
            a, executor, d.SessionOptions(preset_association=True, security_backend_factory=factory)
        )
    assert calls == []
    with pytest.raises(ValueError, match="caller-driven"):
        d.Client(d.ClientOptions(protocol=d.SessionOptions(security_backend_factory=factory)))
    with pytest.raises(ValueError, match="caller-driven"):
        d.Server(
            options=d.ServerOptions(protocol=d.SessionOptions(security_backend_factory=factory))
        )
    assert calls == []
    a.close()
    b.close()
    executor.run_ready()


def test_listening_engine_creates_independent_backends_for_two_live_connections():
    backends = []
    owners = []

    def factory():
        owners.append(threading.get_ident())
        backend = MockBackend()
        backends.append(backend)
        return backend

    registry = n.ObjectRegistry()
    obj = n.MemoryObject()
    obj.set(2, d.Data.uint16(7))
    registry.register_object(
        n.ObjectSchema(oi=0x4500, attributes=[n.AttributeSchema(number=2, type=d.DataType.uint16)]),
        obj,
    )
    server = n.Engine(d.SessionOptions(security_backend_factory=factory), registry)
    first = n.Engine(d.SessionOptions(security_backend=MockBackend()))
    second = n.Engine(d.SessionOptions(security_backend=MockBackend()))
    active = [server, first, second]

    def wait(engine, token):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            for current in active:
                for event in current.poll():
                    if current is engine and event.token == token:
                        assert event.error is None
                        return event
        raise AssertionError("secure connection did not complete")

    try:
        port = server.listen("127.0.0.1", 0, d.ConnectionProfile.local_public)
        for client in (first, second):
            wait(client, client.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_public))
            wait(client, client.connect())
        assert len(backends) == 2 and backends[0] is not backends[1]
        assert owners == [threading.get_ident()] * 2
        resets = [backend.resets for backend in backends]
        first.close()
        active.remove(first)
        response = wait(second, second.get([d.Oad(oi=0x4500, attribute=2)])).message
        assert response.attributes[0].result.as_uint16() == 7
        assert backends[0].resets > resets[0] and backends[1].resets == resets[1]
    finally:
        second.close()
        first.close()
        server.close()


def test_security_reset_exception_is_reported_without_crossing_noexcept(monkeypatch):
    failures = []
    monkeypatch.setattr(sys, "unraisablehook", failures.append)

    class BadReset(MockBackend):
        def reset(self):
            raise RuntimeError("cleanup failed")

    executor = n.ManualExecutor()
    a, b = n.MemoryChannel.pair(executor)
    session = n.SessionHandle(a, executor, d.SessionOptions(security_backend=BadReset()))
    session.request_close()
    executor.run_ready()
    del session
    gc.collect()
    assert failures and all("cleanup failed" in str(item.exc_value) for item in failures)
    b.close()
    executor.run_ready()
