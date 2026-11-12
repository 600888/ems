"""异步完成、取消、同一原生会话与 loop 生命周期。"""

import asyncio
import socket
import threading

import pytest

import dlt698 as d
from dlt698 import _native as n


@pytest.mark.asyncio
async def test_async_read_write_partial_and_close():
    attribute = d.Oad(oi=0x200F, attribute=2)
    with d.Server() as server:
        server.set(attribute, d.Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        async with d.AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            assert (await client.get(attribute)).require_data().as_uint16() == 5000
            result = await client.get_list([attribute, d.Oad(oi=0xF002, attribute=2)])
            assert result.attributes[1].result == 4
            assert await client.set(attribute, d.Data.uint16(4000)) == 3
        await client.aclose()


@pytest.mark.asyncio
async def test_async_preset_profile_and_loop_keeps_running():
    with d.Server() as server:
        server.start_tcp("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        async with d.AsyncClient() as client:
            await client.connect_tcp(
                "127.0.0.1", server.local_port, d.ConnectionProfile.local_preset
            )
            result = await client.get(d.Oad(oi=0xF003, attribute=2))
            assert result.dar == 4
            event = asyncio.Event()
            asyncio.get_running_loop().call_soon(event.set)
            await asyncio.wait_for(event.wait(), timeout=1)


@pytest.mark.asyncio
async def test_failed_connection_can_be_retried():
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        unused = reserved.getsockname()[1]
    # macOS 对已绑定但未监听的端口会等待超时；先关闭占位 socket 才能触发连接拒绝。
    async with d.AsyncClient() as client:
        with pytest.raises(d.Dlt698Error) as failure:
            await client.connect_tcp("127.0.0.1", unused)
        assert failure.value.code == d.ErrorCode.io_error
        with d.Server() as server:
            server.start_tcp("127.0.0.1", 0)
            await client.connect_tcp("127.0.0.1", server.local_port)
            assert (await client.get(d.Oad(oi=0xFFFF, attribute=2))).dar == 4


@pytest.mark.asyncio
async def test_task_cancel_and_loop_close_leave_no_callbacks():
    received = asyncio.Event()
    finished = asyncio.Event()
    events = []

    async def peer(reader, writer):
        try:
            assert await reader.read(65536)
            received.set()
            await reader.read()
        finally:
            writer.close()
            await writer.wait_closed()
            finished.set()

    listener = await asyncio.start_server(peer, "127.0.0.1", 0)
    async with listener, d.AsyncClient(on_event=events.append) as client:
        port = listener.sockets[0].getsockname()[1]
        await client.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_preset)
        task = asyncio.create_task(client.get(d.Oad(oi=0x200F, attribute=2)))
        await asyncio.wait_for(received.wait(), 2)
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
        await client.aclose()
        before = len(events)
        await asyncio.wait_for(finished.wait(), 2)
        await asyncio.sleep(0)
        assert len(events) == before
    await client.aclose()


@pytest.mark.asyncio
async def test_async_serial_failure_retry_probe_disconnect_and_no_python_threads(monkeypatch):
    def forbidden(*args, **kwargs):
        raise AssertionError("Python worker thread creation is forbidden")

    monkeypatch.setattr(threading.Thread, "start", forbidden)
    monkeypatch.setattr(asyncio, "to_thread", forbidden)
    monkeypatch.setattr(asyncio.get_running_loop(), "run_in_executor", forbidden)
    events = []
    async with d.AsyncServer() as server, d.AsyncClient(on_event=events.append) as client:
        with pytest.raises(d.Dlt698Error):
            await client.open_serial("COM9999")
        with pytest.raises(d.Dlt698Error) as failure:
            await client.open_serial_configured(
                "COM9999",
                n.SerialOptions(stop_bits=n.SerialStopBits.one_point_five),
                n.SerialLinkOptions(),
            )
        assert failure.value.code == d.ErrorCode.unsupported_service
        with pytest.raises(d.Dlt698Error) as failure:
            await client.open_serial_configured(
                "COM9999", n.SerialOptions(), n.SerialLinkOptions(set_transmit=lambda tx: None)
            )
        assert failure.value.code == d.ErrorCode.invalid_value
        frequency = d.Oad(oi=0x200F, attribute=2)
        server.set(frequency, d.Data.uint16(5000))
        await server.start_tcp("127.0.0.1", 0)
        await client.connect_tcp("127.0.0.1", server.local_port)
        points = await client.probe_points([frequency, d.Oad(oi=0xFFFF, attribute=2), frequency])
        assert points[0].outcome.as_uint16() == 5000 and points[1].outcome == 4
        assert client.capabilities.negotiated is not None
        await client.disconnect()
        assert client.capabilities.negotiated is None
        await client.connect_tcp("127.0.0.1", server.local_port)
        assert (await client.get(frequency)).require_data().as_uint16() == 5000
        assert any(event.traffic is not None for event in events)
        await client.aclose()
        before = len(events)
        await asyncio.sleep(0.005)
        assert len(events) == before


@pytest.mark.asyncio
async def test_disconnect_cancels_inflight_request_and_rejects_competing_connect():
    async with d.AsyncServer() as server, d.AsyncClient() as client:
        await server.start_tcp("127.0.0.1", 0)
        await client.connect_tcp("127.0.0.1", server.local_port)

        request = asyncio.create_task(client.get(d.Oad(oi=0xFFFF, attribute=2)))
        await asyncio.sleep(0)
        releasing = asyncio.create_task(client.disconnect())
        await asyncio.sleep(0)
        with pytest.raises(d.Dlt698Error) as failure:
            await client.connect_tcp("127.0.0.1", server.local_port)
        assert failure.value.code == d.ErrorCode.busy
        await releasing
        result = (await asyncio.gather(request, return_exceptions=True))[0]
        assert (
            result.dar == 4
            if isinstance(result, d.ReadResult)
            else result.code == d.ErrorCode.cancelled
        )
        await client.connect_tcp("127.0.0.1", server.local_port)


@pytest.mark.asyncio
async def test_disconnect_finishes_old_probe_before_reused_tokens_can_affect_reconnect():
    async with d.AsyncServer() as server, d.AsyncClient() as client:
        await server.start_tcp("127.0.0.1", 0)
        await client.connect_tcp("127.0.0.1", server.local_port)
        task = asyncio.create_task(
            client.probe_points([d.Oad(oi=0xFFFF, attribute=2)] * 128, n.ProbeOptions(batch_size=1))
        )
        await asyncio.sleep(0)
        await client.disconnect()
        await client.connect_tcp("127.0.0.1", server.local_port)
        async with asyncio.timeout(2):
            result = (await asyncio.gather(task, return_exceptions=True))[0]
        assert isinstance(result, (list, d.Dlt698Error))
        assert (await client.get(d.Oad(oi=0xFFFF, attribute=2))).dar == 4


@pytest.mark.parametrize(
    "profile", [d.ConnectionProfile.remote_public, d.ConnectionProfile.local_preset]
)
@pytest.mark.asyncio
async def test_consecutive_large_reads_reuse_piid_on_same_connection(profile):
    """每轮超过 PIID 空间；同一 TCP 连接必须连续读到最新值。"""
    attribute = d.Oad(oi=0x200F, attribute=2)
    with d.Server() as server:
        server.set(attribute, d.Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0, profile)
        async with d.AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port, profile)
            engine = client._engine
            for round_number in range(5):
                value = 5000 + round_number
                server.set(attribute, d.Data.uint16(value))
                points = await asyncio.wait_for(client.probe_points([attribute] * 1000), timeout=5)
                assert len(points) == 1000
                assert all(point.outcome.as_uint16() == value for point in points)
                assert client._engine is engine
