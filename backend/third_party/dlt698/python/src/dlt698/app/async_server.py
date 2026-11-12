"""asyncio 服务端入口，复用 C++ 托管服务器的异步接入与协议会话。"""

from __future__ import annotations

import asyncio
import threading
from collections.abc import Callable
from types import TracebackType
from typing import Self

from .. import _native as n
from ..common.errors import _from_native
from .server import Server


class AsyncServer:
    """在所属 loop 使用的 TCP/串口服务端，设备可与同步服务器共享。

    接入、会话和协议 I/O 沿用 C++ app::Server 的工作线程；启动及停止等待线程
    也由 C++ ServerRunner 创建和回收，Python 只轮询完成通知，不创建工作线程。
    Python 事件回调只在所属 loop 分发，必须及时返回。
    启动取消后等待实际启动结束并停止，避免迟到的监听器泄漏；stop 后可重新启动，
    close/aclose 是终止入口。退出 loop 前必须 await aclose()。
    同步托管服务器对 Python 安全后端的限制同样适用。
    """

    def __init__(
        self,
        device: n.Device | None = None,
        options: n.ServerOptions | None = None,
        *,
        on_event: Callable[[n.Event], None] | None = None,
    ):
        self._loop = asyncio.get_running_loop()
        self._owner = threading.get_ident()
        self._server = Server(device, options, on_event=on_event)
        self._runner = n.ServerRunner(self._server._native)
        self._pending: asyncio.Future[None] | None = None
        self._lock = asyncio.Lock()
        self._handle: asyncio.TimerHandle | None = None
        self._close_task: asyncio.Task[None] | None = None
        self._closing = False
        self._stop_requested = False
        self._on_event = on_event

    def _check_loop(self, *, allow_closing: bool = False) -> None:
        if threading.get_ident() != self._owner or asyncio.get_running_loop() is not self._loop:
            raise RuntimeError("AsyncServer requires its owner loop")
        if self._closing and not allow_closing:
            raise RuntimeError("AsyncServer is closed")

    def _schedule(self) -> None:
        if self._handle is None and (
            self._pending is not None
            or (
                not self._closing
                and self._on_event is not None
                and self._server.state == n.ServerState.running
            )
        ):
            self._handle = self._loop.call_later(0.001, self._dispatch)

    def _dispatch(self) -> None:
        self._handle = None
        try:
            completion = self._runner.poll()
            if completion is not None and self._pending is not None:
                future, self._pending = self._pending, None
                if not future.done():
                    error = completion.error
                    if error is None:
                        future.set_result(None)
                    else:
                        future.set_exception(
                            _from_native(
                                error.code,
                                error.offset,
                                error.context,
                                error.remote_code,
                                error.context_bytes,
                            )
                        )
            if not self._closing:
                self._server.dispatch_events()
        except Exception as error:
            self._loop.call_exception_handler(
                {"message": "dlt698 server callback failed", "exception": error}
            )
        finally:
            self._schedule()

    def _cancel_dispatch(self) -> None:
        if self._handle is not None:
            self._handle.cancel()
            self._handle = None

    async def _join_worker(self, task: asyncio.Task[None]) -> None:
        # Python 任务取消不能中断 C++ 调用；反复取消也必须等它退出后才能释放资源。
        while not task.done():
            try:
                await asyncio.shield(task)
            except asyncio.CancelledError:
                continue
        task.result()

    async def _wait_native(self, operation: Callable[[], int]) -> None:
        operation()
        self._pending = self._loop.create_future()
        self._schedule()
        await self._pending

    async def _operate(self, operation: Callable[[], int], *, starting: bool = False) -> None:
        self._check_loop()
        if starting and self._lock.locked():
            raise _from_native(n.ErrorCode.busy, 0, "server is changing state", None)
        async with self._lock:
            self._check_loop()
            if starting:
                self._stop_requested = False
            worker = asyncio.create_task(self._wait_native(operation))
            try:
                await asyncio.shield(worker)
                if starting and self._stop_requested:
                    worker = asyncio.create_task(self._wait_native(self._runner.stop))
                    await asyncio.shield(worker)
            except asyncio.CancelledError as cancelled:
                self._server.request_stop()
                try:
                    await self._join_worker(worker)
                except Exception as error:
                    cancelled.add_note(
                        f"dlt698 server operation failed during cancellation: {error}"
                    )
                # request_stop 可能早于真正绑定端口；完成调用后再 stop，覆盖迟到的启动。
                cleanup = asyncio.create_task(self._wait_native(self._runner.stop))
                try:
                    await self._join_worker(cleanup)
                except Exception as error:
                    cancelled.add_note(f"dlt698 server cleanup failed: {error}")
                raise
            finally:
                self._cancel_dispatch()
                self._schedule()

    def set(self, attribute: n.Oad, value: n.Data) -> None:
        """本地原子发布数据；不执行网络等待，不授予远端 SET 权限。"""
        self._check_loop()
        self._server.set(attribute, value)

    async def start_tcp(
        self,
        address: str,
        port: int,
        profile: n.ConnectionProfile = n.ConnectionProfile.remote_public,
    ) -> None:
        """等待原生监听就绪；port=0 自动分配，取消时确定性停止迟到的启动。"""
        await self._operate(lambda: self._runner.start_tcp(address, port, profile), starting=True)

    async def start_serial(
        self,
        path: str,
        baud: int = 9600,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """异步等待默认 8E1 串口入口就绪；原生处理帧间时序。"""
        await self._operate(lambda: self._runner.start_serial(path, baud, profile), starting=True)

    async def start_serial_configured(
        self,
        path: str,
        serial: n.SerialOptions,
        link: n.SerialLinkOptions,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """使用完整速率、字格式、流控、通道预算和帧间隔配置启动。"""
        await self._operate(
            lambda: self._runner.start_serial_configured(path, serial, link, profile),
            starting=True,
        )

    def request_stop(self) -> None:
        """非阻塞请求停止；await stop() 确认收尾完成后才能重新启动。"""
        self._check_loop(allow_closing=True)
        self._stop_requested = True
        self._cancel_dispatch()
        self._server.request_stop()
        self._schedule()

    async def stop(self) -> None:
        """等待所有原生 I/O 和工作线程退出；设备数据保留，随后可以重新启动。"""
        self._check_loop(allow_closing=True)
        if self._closing:
            await self.aclose()
            return
        await self._operate(self._runner.stop)

    def close(self) -> None:
        """立即禁用回调并请求停止；在所属 loop 发起收尾，随后必须 await aclose()。"""
        self._check_loop(allow_closing=True)
        if self._closing:
            return
        self._closing = True
        self._cancel_dispatch()
        self._server._disable_events()
        self._server.request_stop()
        # 启动正在等待完成时仍须轮询原生结果，收尾任务才能取得启停锁。
        self._schedule()
        self._close_task = self._loop.create_task(self._finish_close())

    async def _finish_close(self) -> None:
        async with self._lock:
            await self._wait_native(self._runner.stop)

    async def aclose(self) -> None:
        """幂等等待终止收尾；调用者取消不会取消仍在执行的原生停止操作。"""
        self.close()
        assert self._close_task is not None
        await asyncio.shield(self._close_task)

    @property
    def device(self) -> n.Device:
        """与服务器共享所有权的设备，停止和关闭不清空已发布数据。"""
        return self._server.device

    @property
    def state(self) -> n.ServerState:
        """原生入口运行状态，不表示客户端已完成关联。"""
        return self._server.state

    @property
    def local_port(self) -> int:
        """实际监听端口；停止或串口模式为零。"""
        return self._server.local_port

    @property
    def connections(self) -> int:
        """原生最近发布的活动连接数，受 ServerOptions.max_connections 限制。"""
        return self._server.connections

    @property
    def dropped_events(self) -> int:
        """有界诊断队列因条数或字节预算超限丢弃的事件总数。"""
        return self._server.dropped_events

    async def __aenter__(self) -> Self:
        self._check_loop()
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        try:
            await self.aclose()
        except Exception as cleanup_error:
            if exc is None:
                raise
            exc.add_note(f"dlt698 async server close failed: {cleanup_error}")
