"""原生手动执行器、自定义扩展点、一次性完成与手动 RS-485 控制。"""

import gc
import sys
import threading
import time
import weakref

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import (
    IChannel,
    IExecutor,
    IoRuntime,
    ITimer,
    ManualExecutor,
    MemoryChannel,
    MemoryObject,
    ObjectRegistry,
    SerialLinkChannel,
    SerialLinkOptions,
    SessionHandle,
    TcpChannel,
    TcpListener,
    attach_services,
)


def test_memory_channel_budget_close_and_cancelled_timer():
    executor = ManualExecutor()
    tasks = []
    timer = executor.schedule(0.001, lambda: tasks.append("cancelled"))
    timer.cancel()
    executor.schedule(0.0001, lambda: tasks.append("fractional"))
    executor.post(lambda: tasks.append("posted"))
    a, b = MemoryChannel.pair(executor, n.MemoryOptions(read_chunk_bytes=2, max_buffer_bytes=3))
    reads, writes = [], []
    b.async_read(reads.append)
    a.async_write(b"abc", writes.append)
    assert reads == writes == []
    executor.run_ready()
    assert reads == [b"ab"] and writes == [None]
    a.async_write(b"defg", writes.append)
    executor.run_ready()
    assert writes[-1].code == d.ErrorCode.resource_limit
    b.async_read(reads.append)
    executor.run_ready()
    b.async_read(reads.append)
    a.close()
    b.close()
    executor.advance(0.002)
    assert tasks == ["posted", "fractional"] and reads[-1].code == d.ErrorCode.closed
    for delay in [-1, float("nan"), float("inf"), 1e15]:
        with pytest.raises(ValueError):
            executor.schedule(delay, lambda: None)
        with pytest.raises(ValueError):
            executor.advance(delay)


class Timer(ITimer):
    def __init__(self, native):
        super().__init__()
        self.native = native

    def cancel(self):
        self.native.cancel()


class Executor(IExecutor):
    def __init__(self):
        super().__init__()
        self.native = ManualExecutor()

    def post(self, task):
        self.native.post(task)

    def schedule(self, delay, task):
        return Timer(self.native.schedule(delay, task))

    def now(self):
        return self.native.now()

    def is_current(self):
        return self.native.is_current()


class Channel(IChannel):
    def __init__(self, native):
        super().__init__()
        self.native = native
        self.completed = []

    def async_read(self, callback):
        self.native.async_read(callback)

    def async_write(self, data, callback):
        self.native.async_write(data, callback)
        self.completed.append(callback)

    def close(self):
        self.native.close()


def test_custom_channel_executor_session_clock_and_provider_convenience():
    executor = Executor()
    a, b = MemoryChannel.pair(executor)
    channel = Channel(a)
    clocks = []

    def clock():
        clocks.append(threading.get_ident())
        return n.DateTime(value=[7, 234, 10, 7, 3, 0, 0, 0, 0, 0])

    client = SessionHandle(channel, executor, n.SessionOptions(calendar_clock=clock))
    server = SessionHandle(b, executor, n.SessionOptions(role=n.Role.server, calendar_clock=clock))
    registry, obj = ObjectRegistry(), MemoryObject()
    obj.set(2, d.Data.uint16(42))
    retained = []

    def method(omd, parameter):
        retained.extend([omd, parameter])
        return n.ActionValue(dar=0, data=d.Data.uint16(43))

    obj.bind_method(1, method)
    obj.bind_record(3, lambda query: n.RecordResult(attribute=query.attribute, result=4))
    assert obj.read_record(n.GetRecord(attribute=d.Oad(oi=0xF001, attribute=3))).result == 4
    registry.register_object(
        n.ObjectSchema(
            oi=0xF001,
            attributes=[n.AttributeSchema(number=2, type=d.DataType.uint16)],
            methods=[n.MethodSchema(number=1, parameter_type=d.DataType.null)],
        ),
        obj,
    )
    attach_services(server, registry)
    client.start()
    server.start()
    executor.native.run_ready()
    connected = []
    client.async_connect(connected.append)
    executor.native.run_ready()
    assert connected[0].result == 0
    traffic, results = [], []
    client.set_traffic_handler(traffic.append)
    client.async_action([n.ActionMethod(method=d.Omd(oi=0xF001, method=1))], False, results.append)
    executor.native.run_ready()
    assert results[0].methods[0].data.as_uint16() == 43
    assert clocks and set(clocks) == {threading.get_ident()}
    assert retained[0].oi == 0xF001 and retained[1].type == d.DataType.null
    assert {event.kind for event in traffic} == {"send", "receive"}
    with pytest.raises(ValueError, match="already delivered"):
        channel.completed[-1](None)
    # 注册目录和会话保活 Python provider、通道及执行器；不依赖局部 Python 引用。
    del obj
    channel_ref = weakref.ref(channel)
    del channel
    gc.collect()
    assert channel_ref() is not None
    read = []
    client.async_get([d.Oad(oi=0xF001, attribute=2)], False, read.append)
    executor.native.run_ready()
    assert read[0].attributes[0].result.as_uint16() == 42
    released = []
    client.async_release(released.append)
    executor.native.run_ready()
    assert released == [None]
    client.request_close()
    server.request_close()
    executor.native.run_ready()
    assert traffic[0].bytes


def test_custom_channel_io_runtime_reacquires_gil_and_isolates_late_completion():
    runtime = IoRuntime()
    executor = runtime.executor()
    a, b = MemoryChannel.pair(executor)
    channel = Channel(a)
    client = SessionHandle(channel, executor)
    server = SessionHandle(b, executor, n.SessionOptions(role=n.Role.server))
    client.start()
    server.start()
    results = []
    client.async_connect(results.append)
    for _ in range(10):
        runtime.run_for()
        if results:
            break
    assert results[0].result == 0
    client.request_close()
    server.request_close()
    runtime.finish()
    for _ in range(10):
        runtime.run_for()
    assert client.state == n.SessionState.closed

    class Failed(Channel):
        def async_write(self, data, callback):
            self.completed.append(callback)
            raise ValueError("driver write failed")

    manual = ManualExecutor()
    a, b = MemoryChannel.pair(manual)
    failed = Failed(a)
    client = SessionHandle(failed, manual)
    client.start()
    results = []
    client.async_connect(results.append)
    manual.run_ready()
    assert len(results) == 1 and results[0].code == d.ErrorCode.io_error
    failed.completed[0](None)  # 已失败的驱动操作晚到完成，不能二次通知原生事务。
    manual.run_ready()
    assert len(results) == 1
    with pytest.raises(ValueError, match="already delivered"):
        failed.completed[0](None)
    client.request_close()
    b.close()
    manual.run_ready()


def test_manual_rs485_waits_for_real_drain_and_restores_receive():
    executor = ManualExecutor()
    a, b = MemoryChannel.pair(executor)
    directions, drains, writes, reads = [], [], [], []
    link = SerialLinkOptions(
        set_transmit=lambda tx: directions.append(tx), async_drain=drains.append
    )
    channel = SerialLinkChannel.wrap(a, executor, link)
    frame = n.encode_frame(n.Frame(control=0x43, payload=b"\x00"))
    b.async_read(reads.append)
    channel.async_write(frame, writes.append)
    executor.advance(0.004)
    assert directions[-1] is True and reads[0] == b"\xfe" * 4 + frame
    assert writes == [] and len(drains) == 1
    drains[0](None)
    executor.run_ready()
    executor.advance(0.004)
    assert writes == [None] and directions[-1] is False
    with pytest.raises(ValueError, match="already delivered"):
        drains[0](None)
    channel.close()
    b.close()
    executor.run_ready()
    assert directions[-1] is False
    with pytest.raises(ValueError):
        SerialLinkChannel.wrap(a, executor, SerialLinkOptions(set_transmit=lambda tx: None))


def test_rs485_close_during_drain_cancels_write_and_ignores_late_hardware_completion():
    executor = ManualExecutor()
    a, b = MemoryChannel.pair(executor)
    directions, drains, writes = [], [], []
    channel = SerialLinkChannel.wrap(
        a, executor, SerialLinkOptions(set_transmit=directions.append, async_drain=drains.append)
    )
    channel.async_write(n.encode_frame(n.Frame(control=0x43, payload=b"\x00")), writes.append)
    executor.run_ready()

    assert len(drains) == 1 and writes == []
    channel.close()
    executor.run_ready()
    assert len(writes) == 1 and writes[0].code == d.ErrorCode.closed
    assert directions[-1] is False
    drains[0](None)
    executor.advance(1.0)
    assert len(writes) == 1
    b.close()
    executor.run_ready()


def test_custom_executor_preserves_fractional_millisecond_rs485_gap():
    executor = Executor()
    a, b = MemoryChannel.pair(executor)
    directions, writes = [], []
    channel = SerialLinkChannel.wrap(
        a,
        executor,
        SerialLinkOptions(set_transmit=directions.append, async_drain=lambda done: done(None)),
    )
    channel.async_write(n.encode_frame(n.Frame(control=0x43, payload=b"\x00")), writes.append)
    executor.native.run_ready()
    assert directions[-1] is False and writes == []
    executor.native.advance(33 / 9600 - 0.000000001)
    assert writes == []
    executor.native.advance(0.000000002)
    assert writes == [None]
    channel.close()
    b.close()
    executor.native.run_ready()


def test_lowlevel_native_tcp_and_serial_failure_are_caller_driven():
    runtime = IoRuntime()
    listener = TcpListener.listen(runtime, "127.0.0.1", 0)
    accepted, connected = [], []
    listener.async_accept(accepted.append)
    channel = TcpChannel.connect(runtime, "127.0.0.1", listener.local_port, connected.append)
    deadline = time.monotonic() + 3
    while not accepted or not connected:
        assert time.monotonic() < deadline
        runtime.run_for()
    assert connected == [None] and isinstance(accepted[0], TcpChannel)
    reads, writes = [], []
    accepted[0].async_read(reads.append)
    data = bytearray(b"native tcp")
    channel.async_write(data, writes.append)
    data[0] = 0
    while not reads or not writes:
        assert time.monotonic() < deadline
        runtime.run_for()
    assert reads == [b"native tcp"] and writes == [None]
    with pytest.raises(d.Dlt698Error):
        n.SerialChannel.open(runtime, "COM9999")
    listener.close()
    channel.close()
    accepted[0].close()
    runtime.finish()
    for _ in range(10):
        runtime.run_for()


def test_missing_close_cancel_overrides_report_errors_without_crossing_native_destructors(
    monkeypatch,
):
    reports = []
    monkeypatch.setattr(sys, "unraisablehook", reports.append)

    class MissingTimer(ITimer):
        pass

    class MissingChannel(IChannel):
        pass

    timer, channel = MissingTimer(), MissingChannel()
    timer.cancel()
    channel.close()
    assert len(reports) == 2
    assert {str(item.object) for item in reports} == {"ITimer.cancel", "IChannel.close"}
