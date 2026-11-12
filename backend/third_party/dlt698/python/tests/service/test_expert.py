"""原生高级会话、Python provider 生命周期及副作用语义。"""

import gc
import hashlib
import threading
import time

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import Engine, ObjectProvider, ObjectRegistry


class Provider(ObjectProvider):
    def __init__(self):
        super().__init__()
        self.value = d.Data.uint16(7)
        self.calls = 0
        self.threads = []
        self.retained = []

    def read(self, attribute):
        self.threads.append(threading.get_ident())
        self.retained.append(attribute)
        if attribute.index == 99:
            raise ValueError("provider failure")
        return self.value

    def write(self, attribute, value):
        self.value = value
        return 0

    def invoke(self, method, parameter):
        self.calls += 1
        return n.ActionValue(dar=0, data=self.value)


class Peers:
    def __init__(self, *, public=False, client_options=None, server_options=None):
        self.provider = Provider()
        self.registry = ObjectRegistry()
        self.registry.register_object(
            n.ObjectSchema(
                oi=0x4500,
                attributes=[n.AttributeSchema(number=2, type=d.DataType.uint16, writable=True)],
                methods=[n.MethodSchema(number=1, parameter_type=d.DataType.null)],
            ),
            self.provider,
        )
        self.server = Engine(server_options or d.SessionOptions(), self.registry)
        self.client = Engine(client_options or d.SessionOptions())
        profile = d.ConnectionProfile.local_public if public else d.ConnectionProfile.local_preset
        port = self.server.listen("127.0.0.1", 0, profile)
        self.wait(self.client.connect_tcp("127.0.0.1", port, profile))
        if public:
            assert self.wait(self.client.connect()).message.result == 0

    def wait(self, token):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.server.poll()
            for event in self.client.poll():
                if event.token == token:
                    assert event.error is None, event.error.context if event.error else ""
                    return event
        raise AssertionError("native operation did not complete before deadline")

    def close(self):
        self.client.close()
        self.server.close()


@pytest.fixture
def peers():
    pair = Peers()
    try:
        yield pair
    finally:
        pair.close()


def test_provider_partial_write_action_and_exception(peers):
    attribute = d.Oad(oi=0x4500, attribute=2)
    result = peers.wait(
        peers.client.set(
            [
                n.SetAttribute(attribute=attribute, value=d.Data.uint16(8)),
                n.SetAttribute(attribute=d.Oad(oi=0xFFFF, attribute=2), value=d.Data.uint16(9)),
            ],
            True,
        )
    ).message
    assert [item.dar for item in result.attributes] == [0, 4]
    assert peers.provider.value.as_uint16() == 8
    response = peers.wait(
        peers.client.action(
            [n.ActionMethod(method=d.Omd(oi=0x4500, method=1), parameter=d.Data.null())]
        )
    ).message
    assert response.methods[0].data.as_uint16() == 8
    assert peers.provider.calls == 1
    result = peers.wait(peers.client.get([d.Oad(oi=0x4500, attribute=2, index=99)])).message
    assert result.attributes[0].result == 255
    assert peers.provider.threads == [threading.get_ident()]
    assert peers.provider.retained[0].index == 99


def test_provider_retained_parameters_own_their_memory(peers):
    attribute = d.Oad(oi=0x4500, attribute=2)
    peers.wait(peers.client.get([attribute]))
    retained = peers.provider.retained[0]
    for _ in range(4):
        peers.wait(peers.client.get([d.Oad(oi=0x4500, attribute=2, index=1)]))
    assert retained == attribute
    peers.close()
    gc.collect()
    assert retained.oi == 0x4500 and retained.index == 0


def test_engine_queue_overflow_fails_explicitly():
    registry = ObjectRegistry()
    server = Engine(objects=registry)
    client = Engine(queue_limit=1, queue_bytes=1)
    try:
        port = server.listen("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        client.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_preset)
        with pytest.raises(RuntimeError, match="queue overflow"):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                server.poll()
                client.poll()
    finally:
        client.close()
        server.close()


def test_native_md5_and_then_get(peers):
    attribute = d.Oad(oi=0x4500, attribute=2)
    response = peers.wait(peers.client.exchange(n.GetMd5Request(attribute=attribute))).message
    assert bytes(response.result) == hashlib.md5(d.encode_data(d.Data.uint16(7))).digest()
    result = peers.wait(
        peers.client.exchange(
            n.SetThenGetRequest(
                items=[
                    n.SetThenGet(
                        set=n.SetAttribute(attribute=attribute, value=d.Data.uint16(9)),
                        read=attribute,
                        delay_seconds=0,
                    )
                ]
            )
        )
    ).message
    assert result.items[0].set.dar == 0
    assert result.items[0].read.result.as_uint16() == 9


def test_smart_holder_keeps_python_provider_override_alive(peers):
    peers.provider = None
    gc.collect()
    result = peers.wait(peers.client.get([d.Oad(oi=0x4500, attribute=2)])).message
    assert result.attributes[0].result.as_uint16() == 7


def test_owner_thread_and_close_guard(peers):
    errors = []

    def wrong_thread():
        try:
            peers.client.poll()
        except RuntimeError as error:
            errors.append(str(error))

    thread = threading.Thread(target=wrong_thread)
    thread.start()
    thread.join(timeout=2)
    assert not thread.is_alive()
    assert errors == ["Engine requires its owner thread"]
    peers.client.close()
    peers.client.close()
    with pytest.raises(RuntimeError, match="closed"):
        peers.client.poll()
