"""原生异步服务器复用、loop 生命周期、取消与资源收尾。"""

import asyncio
import socket
import threading

import pytest

import dlt698 as d
from dlt698 import _native as n


async def eventually(predicate):
    async with asyncio.timeout(3):
        while not predicate():
            await asyncio.sleep(0.001)


@pytest.mark.asyncio
@pytest.mark.parametrize("profile", list(d.ConnectionProfile.__members__.values()))
async def test_native_async_server_multi_client_profiles_and_restart(profile):
    attribute = d.Oad(oi=0x200F, attribute=2)
    device = d.Device()
    async with d.AsyncServer(device) as server:
        assert isinstance(server._server._native, n.NativeServer)
        assert isinstance(server._runner, n.ServerRunner)
        assert server.device is device
        server.set(attribute, d.Data.uint16(5000))
        await server.start_tcp("127.0.0.1", 0, profile)
        assert server.local_port and server.state == d.ServerState.running

        async def read():
            async with d.AsyncClient() as client:
                await client.connect_tcp("127.0.0.1", server.local_port, profile)
                assert (await client.get(attribute)).require_data().as_uint16() == 5000
                assert await client.set(attribute, d.Data.uint16(7)) == 3

        await asyncio.gather(*(read() for _ in range(3)))
        await eventually(lambda: server.connections == 0)
        await server.stop()
        assert server.state == d.ServerState.stopped and server.local_port == 0
        assert device.get(attribute).as_uint16() == 5000
        await server.start_tcp("127.0.0.1", 0, profile)
        await read()
    await server.aclose()
    with pytest.raises(RuntimeError, match="closed"):
        await server.start_tcp("127.0.0.1", 0)


@pytest.mark.asyncio
async def test_async_server_never_creates_python_worker_threads(monkeypatch):
    def forbidden(*args, **kwargs):
        raise AssertionError("Python worker threads must not be created")

    monkeypatch.setattr(asyncio, "to_thread", forbidden)
    monkeypatch.setattr(asyncio.get_running_loop(), "run_in_executor", forbidden)
    monkeypatch.setattr(threading.Thread, "start", forbidden)
    async with d.AsyncServer() as server:
        await server.start_tcp("127.0.0.1", 0)
        async with d.AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            assert (await client.get(d.Oad(oi=0xFFFF, attribute=2))).dar == 4
        await server.stop()


@pytest.mark.asyncio
async def test_async_server_start_failure_busy_and_retry():
    async with d.AsyncServer() as server:
        with pytest.raises(d.Dlt698Error):
            await server.start_tcp("invalid-address", 0)
        assert server.state == d.ServerState.stopped
        await server.start_tcp("127.0.0.1", 0)
        port = server.local_port
        with pytest.raises(d.Dlt698Error) as error:
            await server.start_tcp("127.0.0.1", 0)
        assert error.value.code == d.ErrorCode.busy
        assert server.local_port == port
        await server.stop()
        with socket.socket() as occupied:
            occupied.bind(("127.0.0.1", 0))
            occupied.listen()
            with pytest.raises(d.Dlt698Error):
                await server.start_tcp("127.0.0.1", occupied.getsockname()[1])
        await server.start_tcp("127.0.0.1", 0)


@pytest.mark.asyncio
async def test_async_server_events_stay_on_loop_and_close_in_callback():
    owner = threading.get_ident()
    calls = []

    def on_event(event):
        assert threading.get_ident() == owner
        calls.append(event)
        server.close()

    server = d.AsyncServer(on_event=on_event)
    try:
        await server.start_tcp("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        reader, writer = await asyncio.open_connection("127.0.0.1", server.local_port)
        try:
            await eventually(lambda: server.connections == 1)
            writer.write(b"\x68\xff\xff\x00")
            await writer.drain()
            await eventually(lambda: bool(calls))
            await server.aclose()
            count = len(calls)
            await asyncio.sleep(0.01)
            assert len(calls) == count
            assert server.state == d.ServerState.stopped and server.connections == 0
            assert await asyncio.wait_for(reader.read(), 2) == b""
        finally:
            writer.close()
            await writer.wait_closed()
    finally:
        await server.aclose()


@pytest.mark.asyncio
async def test_start_cancellation_cleans_up_late_native_start(monkeypatch):
    server = d.AsyncServer()
    gate = asyncio.Event()
    entered = asyncio.Event()
    original = server._wait_native
    first = True

    async def delayed(operation):
        nonlocal first
        if first:
            first = False
            entered.set()
            await gate.wait()
        await original(operation)

    monkeypatch.setattr(server, "_wait_native", delayed)
    try:
        task = asyncio.create_task(server.start_tcp("127.0.0.1", 0))
        await entered.wait()
        task.cancel()
        await asyncio.sleep(0)
        task.cancel()  # 重复取消不得打断等待原生操作与收尾。
        gate.set()
        with pytest.raises(asyncio.CancelledError):
            await asyncio.wait_for(task, 3)
        assert server.state == d.ServerState.stopped and server.local_port == 0
        await server.start_tcp("127.0.0.1", 0)
    finally:
        await server.aclose()


@pytest.mark.asyncio
async def test_aclose_cancellation_does_not_interrupt_cleanup(monkeypatch):
    server = d.AsyncServer()
    await server.start_tcp("127.0.0.1", 0)
    gate = asyncio.Event()
    entered = asyncio.Event()
    original = server._wait_native

    async def delayed(operation):
        entered.set()
        await gate.wait()
        await original(operation)

    monkeypatch.setattr(server, "_wait_native", delayed)
    task = asyncio.create_task(server.aclose())
    await entered.wait()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    gate.set()
    await server.aclose()
    assert server.state == d.ServerState.stopped and server.local_port == 0


@pytest.mark.asyncio
async def test_async_serial_start_errors_are_delivered_and_server_can_retry(tmp_path):
    async with d.AsyncServer() as server:
        missing = str(tmp_path / "missing-port")
        with pytest.raises(d.Dlt698Error):
            await server.start_serial(missing)
        with pytest.raises(d.Dlt698Error):
            await server.start_serial_configured(missing, n.SerialOptions(), n.SerialLinkOptions())
        await server.start_tcp("127.0.0.1", 0)


@pytest.mark.asyncio
async def test_native_connection_budget_is_enforced():
    async with d.AsyncServer(options=d.ServerOptions(max_connections=1)) as server:
        await server.start_tcp("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        first_reader, first = await asyncio.open_connection("127.0.0.1", server.local_port)
        try:
            await eventually(lambda: server.connections == 1)
            reader, second = await asyncio.open_connection("127.0.0.1", server.local_port)
            try:
                assert await asyncio.wait_for(reader.read(), 2) == b""
                assert server.connections == 1
            finally:
                second.close()
                await second.wait_closed()
        finally:
            first.close()
            await first.wait_closed()
        await eventually(lambda: server.connections == 0)
        assert await first_reader.read() == b""


@pytest.mark.asyncio
async def test_callback_exception_is_reported_without_stopping_loop():
    observed = asyncio.Event()
    loop = asyncio.get_running_loop()
    previous = loop.get_exception_handler()

    def handler(loop, context):
        assert context["message"] == "dlt698 server callback failed"
        assert isinstance(context["exception"], ValueError)
        observed.set()

    def callback(event):
        raise ValueError("application callback failed")

    loop.set_exception_handler(handler)
    try:
        async with d.AsyncServer(on_event=callback) as server:
            await server.start_tcp("127.0.0.1", 0, d.ConnectionProfile.local_preset)
            async with d.AsyncClient() as client:
                await client.connect_tcp(
                    "127.0.0.1", server.local_port, d.ConnectionProfile.local_preset
                )
                assert (await client.get(d.Oad(oi=0xFFFF, attribute=2))).dar == 4
                await asyncio.wait_for(observed.wait(), 2)
            assert server.state == d.ServerState.running
    finally:
        loop.set_exception_handler(previous)


def test_async_server_rejects_another_loop():
    async def create():
        return d.AsyncServer()

    async def wrong_loop(server):
        with pytest.raises(RuntimeError, match="owner loop"):
            await server.start_tcp("127.0.0.1", 0)

    with asyncio.Runner() as owner, asyncio.Runner() as other:
        server = owner.run(create())
        try:
            other.run(wrong_loop(server))
        finally:
            owner.run(server.aclose())


@pytest.mark.asyncio
@pytest.mark.parametrize("terminal", [False, True])
async def test_stop_or_close_during_pending_start_does_not_lose_completion(terminal):
    server = d.AsyncServer()
    native = server._runner
    gate = False

    class DelayedPoll:
        def __getattr__(self, name):
            return getattr(native, name)

        def poll(self):
            return native.poll() if gate else None

    server._runner = DelayedPoll()
    try:
        startup = asyncio.create_task(server.start_tcp("127.0.0.1", 0))
        await eventually(lambda: server._pending is not None)
        if terminal:
            server.close()
        else:
            server.request_stop()
        gate = True
        await asyncio.wait_for(startup, 3)
        if terminal:
            await asyncio.wait_for(server.aclose(), 3)
        else:
            await server.stop()
            await server.start_tcp("127.0.0.1", 0)
            await server.stop()
        assert server.state == d.ServerState.stopped and server.local_port == 0
    finally:
        gate = True
        await server.aclose()


@pytest.mark.asyncio
async def test_cancelling_start_while_requested_stop_is_pending_waits_for_native_stop():
    server = d.AsyncServer()
    native = server._runner
    allow_start = False
    allow_stop = False
    stopping = asyncio.Event()

    class DelayedStopPoll:
        def __getattr__(self, name):
            return getattr(native, name)

        def stop(self):
            token = native.stop()
            stopping.set()
            return token

        def poll(self):
            allowed = allow_stop if stopping.is_set() else allow_start
            return native.poll() if allowed else None

    server._runner = DelayedStopPoll()
    try:
        startup = asyncio.create_task(server.start_tcp("127.0.0.1", 0))
        await eventually(lambda: server._pending is not None)
        server.request_stop()
        allow_start = True
        await asyncio.wait_for(stopping.wait(), 3)
        startup.cancel()
        await asyncio.sleep(0)
        assert not startup.done()
        allow_stop = True
        with pytest.raises(asyncio.CancelledError):
            await asyncio.wait_for(startup, 3)
        assert server.state == d.ServerState.stopped and server.local_port == 0
        await server.start_tcp("127.0.0.1", 0)
    finally:
        allow_start = allow_stop = True
        await server.aclose()
