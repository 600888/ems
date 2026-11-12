"""托管 TCP 闭环、逐项失败、共享设备与确定性收尾。"""

import subprocess
import sys

import pytest

import dlt698 as d
from dlt698.errors import AssociationError, BusyError

FREQUENCY = d.Oad(oi=0x200F, attribute=2)


def test_tcp_read_update_and_partial_list():
    with d.Server() as server:
        server.set(FREQUENCY, d.Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        with d.Client() as client:
            client.connect_tcp("127.0.0.1", server.local_port)
            assert client.get(FREQUENCY).require_data().as_uint16() == 5000
            server.set(FREQUENCY, d.Data.uint16(4998))
            response = client.get_list([FREQUENCY, d.Oad(oi=0xF001, attribute=2)])
            assert response.list
            assert response.attributes[0].result.as_uint16() == 4998
            assert response.attributes[1].result == 4
            assert client.set(FREQUENCY, d.Data.uint16(4000)) == 3
        assert server.device.get(FREQUENCY).as_uint16() == 4998


def test_shared_device_and_restart():
    device = d.Device()
    device.set(FREQUENCY, d.Data.uint16(5000))
    with d.Server(device) as server, d.Server(device) as other:
        for current in [server, other]:
            current.start_tcp("127.0.0.1", 0)
            with d.Client() as client:
                client.connect_tcp("127.0.0.1", current.local_port)
                assert client.get(FREQUENCY).require_data().as_uint16() == 5000
        server.stop()
        server.start_tcp("127.0.0.1", 0)
        assert server.device.get(FREQUENCY).as_uint16() == 5000


def test_not_connected_busy_and_close():
    with d.Client() as client:
        with pytest.raises(AssociationError):
            client.get(FREQUENCY)
    client.close()
    with d.Server() as server:
        server.start_tcp("127.0.0.1", 0)
        with pytest.raises(BusyError):
            server.start_tcp("127.0.0.1", 0)
    server.close()


def test_events_on_caller_and_no_callbacks_after_close():
    events = []
    with d.Server(on_event=events.append) as server:
        server.set(FREQUENCY, d.Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        with d.Client() as client:
            client.connect_tcp("127.0.0.1", server.local_port)
            client.get(FREQUENCY)
        assert events == []
        assert server.dispatch_events() > 0
        assert any(event.kind == "receive" for event in events)
    before = len(events)
    assert server.dispatch_events() == 0
    assert len(events) == before


def test_process_exit_and_reclamation():
    script = """
from dlt698 import Client, Server, Oad, Data
for _ in range(3):
    s=Server(); s.set(Oad(oi=0x200f, attribute=2), Data.uint16(5000))
    s.start_tcp("127.0.0.1", 0)
    c=Client(); c.connect_tcp("127.0.0.1", s.local_port)
    assert c.get(Oad(oi=0x200f, attribute=2)).require_data().as_uint16()==5000
    del c; del s
"""
    subprocess.run([sys.executable, "-c", script], check=True, timeout=20)
