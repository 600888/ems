"""asyncio 桥接原生 Session 异步完成队列，不包装同步客户端线程。"""

from __future__ import annotations

import asyncio
import threading
from collections.abc import Callable, Sequence
from types import TracebackType
from typing import Self

from .. import _native as n
from ..common.errors import Dlt698Error, _from_native
from .client import ReadResult, _read_result


class AsyncClient:
    """在所属事件循环使用；取消交给原生会话，loop 关闭前必须 aclose。

    每次原生 pump 最多 1ms，不创建额外工作线程。单在途事务和不重试约定沿用 C++。
    """

    def __init__(
        self,
        options: n.ClientOptions | None = None,
        *,
        on_event: Callable[[n.Completion], None] | None = None,
    ):
        self._loop = asyncio.get_running_loop()
        self._owner = threading.get_ident()
        self._options = options or n.ClientOptions()
        self._engine = n.Engine(self._options.protocol)
        self._pending: dict[int, asyncio.Future[n.Completion]] = {}
        self._states: asyncio.Queue[n.SessionState] = asyncio.Queue(maxsize=64)
        self._on_event = on_event
        self._closed = False
        self._associated = False
        self._connecting = False
        self._disconnecting = False
        self._capabilities = n.Capabilities()
        self._handle: asyncio.TimerHandle | None = None
        self._schedule()

    def _check_loop(self) -> None:
        if asyncio.get_running_loop() is not self._loop or self._closed:
            raise RuntimeError("AsyncClient is closed or belongs to another loop")

    def _schedule(self) -> None:
        if not self._closed:
            self._handle = self._loop.call_later(0.001, self._pump)

    def _pump(self) -> None:
        if self._closed:
            return
        try:
            for completion in self._engine.poll():
                future = self._pending.get(completion.token) if completion.token else None
                if future is not None and not future.done():
                    error = completion.error
                    if error is not None:
                        future.set_exception(
                            _from_native(
                                error.code,
                                error.offset,
                                error.context,
                                error.remote_code,
                                error.context_bytes,
                            )
                        )
                    else:
                        future.set_result(completion)
                    self._pending.pop(completion.token, None)
                if completion.state is not None:
                    self._states.put_nowait(completion.state)
                if completion.kind == "closed":
                    self._associated = False
                    error = completion.error
                    for pending in self._pending.values():
                        if not pending.done():
                            pending.set_exception(
                                _from_native(
                                    error.code,
                                    error.offset,
                                    error.context,
                                    error.remote_code,
                                    error.context_bytes,
                                )
                                if error is not None
                                else ConnectionError("remote connection closed")
                            )
                    self._pending.clear()
                if self._on_event is not None and not self._closed:
                    try:
                        self._on_event(completion)
                    except Exception as error:
                        self._loop.call_exception_handler(
                            {"message": "dlt698 callback failed", "exception": error}
                        )
        except Exception as error:
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(error)
            self._pending.clear()
            self.close()
            self._loop.call_exception_handler({"message": "dlt698 pump failed", "exception": error})
        finally:
            self._schedule()

    async def _wait(self, token: int, timeout: float | None = None) -> n.Completion:
        self._check_loop()
        future: asyncio.Future[n.Completion] = self._loop.create_future()
        engine = self._engine
        self._pending[token] = future
        try:
            if timeout is None:
                return await future
            async with asyncio.timeout(timeout):
                return await future
        except BaseException as error:
            if self._pending.get(token) is future:
                self._pending.pop(token, None)
            if (
                not self._closed
                and engine is self._engine
                and isinstance(error, (asyncio.CancelledError, TimeoutError))
            ):
                self._associated = False
                try:
                    self._engine.cancel()
                except IndexError:
                    self._engine.close()  # DNS/socket 尚未完成，禁止晚到回调，由建连路径恢复。
            raise

    async def connect_tcp(
        self, host: str, port: int, profile: n.ConnectionProfile = n.ConnectionProfile.remote_public
    ) -> None:
        """依次等待 TCP、LINK 和 CONNECT，成功后才可读写。"""
        await self._establish(
            lambda: self._engine.connect_tcp(host, port, profile, self._options.channel), profile
        )

    async def open_serial(
        self,
        path: str,
        baud: int = 9600,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """异步等待串口 LINK/CONNECT，采用与原生 Client 一致的默认字格式。"""
        await self.open_serial_configured(
            path,
            n.SerialOptions(baud_rate=baud, channel=self._options.channel),
            n.SerialLinkOptions(),
            profile,
        )

    async def open_serial_configured(
        self,
        path: str,
        serial: n.SerialOptions,
        link: n.SerialLinkOptions,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """配置串口及 RS-485 hooks；Python 回调仅在所属 loop 线程执行。"""
        await self._establish(
            lambda: self._engine.open_serial(path, serial, link, profile), profile
        )

    def _reset_engine(self) -> None:
        self._engine.close()
        # 释放完成可早于分批探测等复合操作；旧 token 不得遗留到新运行时并匹配重用序号。
        for future in self._pending.values():
            if not future.done():
                future.set_exception(_from_native(n.ErrorCode.closed, 0, "connection reset", None))
        self._pending.clear()
        self._engine = n.Engine(self._options.protocol)
        self._states = asyncio.Queue(maxsize=64)
        self._associated = False
        self._capabilities = n.Capabilities()

    async def _establish(self, start: Callable[[], int], profile: n.ConnectionProfile) -> None:
        self._check_loop()
        if self._associated or self._connecting or self._disconnecting:
            raise _from_native(n.ErrorCode.busy, 0, "client already connecting/connected", None)
        self._connecting = True
        try:
            await self._wait(start(), self._options.transport_timeout)
            expected = (
                n.SessionState.associated
                if profile == n.ConnectionProfile.local_preset
                else n.SessionState.preconnected
            )
            async with asyncio.timeout(self._options.login_timeout):
                while await self._states.get() != expected:
                    pass
            if profile != n.ConnectionProfile.local_preset:
                response = (await self._wait(self._engine.connect())).message
                if not isinstance(response, n.ConnectResponse):
                    raise RuntimeError("unexpected CONNECT response")
                if response.result != 0:
                    raise _from_native(
                        n.ErrorCode.association_failed, 0, "CONNECT rejected", response.result
                    )
                self._capabilities = n.capabilities_from_connect(response)
            else:
                self._capabilities = n.Capabilities(negotiated=self._options.protocol.parameters)
            self._associated = True
        except BaseException:
            # 失败后收尾旧运行时，并恢复可重试状态；close() 仍是终止入口。
            if not self._closed:
                self._reset_engine()
            raise
        finally:
            self._connecting = False

    @property
    def capabilities(self) -> n.Capabilities:
        """当前连接的独立能力快照；未连接时为未知。"""
        return n.Capabilities(negotiated=self._capabilities.negotiated)

    async def probe_points(
        self, attributes: Sequence[n.Oad], options: n.ProbeOptions | None = None
    ) -> list[n.PointResult]:
        """按原生能力规划探测，保留逐项 Data、DAR、事务错误及标准校验错误。"""
        self._require_connection()
        result = await self._wait(
            self._engine.probe_points(
                self._capabilities, list(attributes), options or n.ProbeOptions()
            )
        )
        if result.points is None:
            raise RuntimeError("unexpected point probe response")
        return result.points

    async def disconnect(self) -> None:
        """等待原生 RELEASE 后关闭通道；收尾后允许重新连接，失败仍释放资源。"""
        self._check_loop()
        if not self._associated:
            return
        self._associated = False
        self._disconnecting = True
        try:
            await self._wait(self._engine.release())
        finally:
            if not self._closed:
                self._reset_engine()
            self._disconnecting = False

    def _require_connection(self) -> None:
        self._check_loop()
        if not self._associated or self._closed:
            raise _from_native(n.ErrorCode.not_associated, 0, "client not connected", None)

    async def get(self, attribute: n.Oad) -> ReadResult:
        """普通 GET，保留 Data/DAR，原生处理分块。"""
        self._require_connection()
        response = (await self._wait(self._engine.get([attribute], False))).message
        if not isinstance(response, n.GetResponse) or len(response.attributes) != 1:
            raise RuntimeError("unexpected GET response")
        return _read_result(response.attributes[0].result)

    async def get_list(self, attributes: Sequence[n.Oad]) -> n.GetResponse:
        """有序非空列表，保留逐项结果和全部响应字段。"""
        self._require_connection()
        response = (await self._wait(self._engine.get(list(attributes), True))).message
        if not isinstance(response, n.GetResponse):
            raise RuntimeError("unexpected GET response")
        return response

    async def set(self, attribute: n.Oad, value: n.Data) -> int:
        """返回原始 DAR，取消后远端是否执行未知。"""
        self._require_connection()
        response = (
            await self._wait(
                self._engine.set([n.SetAttribute(attribute=attribute, value=value)], False)
            )
        ).message
        if not isinstance(response, n.SetResponse) or len(response.attributes) != 1:
            raise RuntimeError("unexpected SET response")
        return response.attributes[0].dar

    async def set_list(self, attributes: Sequence[n.SetAttribute]) -> n.SetResponse:
        """保留逐项成功/失败，不回滚、不重试。"""
        self._require_connection()
        response = (await self._wait(self._engine.set(list(attributes), True))).message
        if not isinstance(response, n.SetResponse):
            raise RuntimeError("unexpected SET response")
        return response

    async def action(self, method: n.Omd, parameter: n.Data) -> n.ActionValue:
        """返回 DAR 与可选精确 Data，不重试。"""
        self._require_connection()
        response = (
            await self._wait(
                self._engine.action([n.ActionMethod(method=method, parameter=parameter)], False)
            )
        ).message
        if not isinstance(response, n.ActionResponse) or len(response.methods) != 1:
            raise RuntimeError("unexpected ACTION response")
        result = response.methods[0]
        return n.ActionValue(dar=result.dar, data=result.data)

    async def action_list(self, methods: Sequence[n.ActionMethod]) -> n.ActionResponse:
        """异步方法列表保留顺序及部分成功。"""
        self._require_connection()
        response = (await self._wait(self._engine.action(list(methods), True))).message
        if not isinstance(response, n.ActionResponse):
            raise RuntimeError("unexpected ACTION response")
        return response

    async def get_record(self, record: n.GetRecord) -> n.RecordResult:
        """完整记录快照或原始 DAR。"""
        self._require_connection()
        response = (await self._wait(self._engine.get_record([record], False))).message
        if not isinstance(response, n.GetRecordResponse) or len(response.records) != 1:
            raise RuntimeError("unexpected record response")
        return response.records[0]

    async def get_record_list(self, records: Sequence[n.GetRecord]) -> n.GetRecordResponse:
        """记录列表与实际表头，分块沿用原生状态机。"""
        self._require_connection()
        response = (await self._wait(self._engine.get_record(list(records), True))).message
        if not isinstance(response, n.GetRecordResponse):
            raise RuntimeError("unexpected record response")
        return response

    async def exchange(
        self,
        request: n.GetMd5Request
        | n.SetThenGetRequest
        | n.ActionThenGetRequest
        | n.ReportNotification
        | n.ProxyRequest,
    ) -> n.Completion:
        """通过原生 async_exchange 处理高级事务，保留完整完成消息。"""
        self._require_connection()
        return await self._wait(self._engine.exchange(request))

    def close(self) -> None:
        """取消并排空原生 I/O，停止回调；必须在所属 loop 线程调用。"""
        if self._closed:
            return
        if threading.get_ident() != self._owner:
            raise RuntimeError("AsyncClient.close requires its owner thread")
        self._closed = True
        self._associated = False
        self._on_event = None
        if self._handle is not None:
            self._handle.cancel()
        for future in self._pending.values():
            if not future.done():
                future.cancel()
        self._pending.clear()
        self._engine.close()

    async def aclose(self) -> None:
        """确定性收尾后不接受新请求。"""
        if self._closed:
            return
        try:
            # 未完成业务或取消后的强制关闭沿用 close；空闲关联优先释放协议连接。
            if self._associated and not self._pending:
                try:
                    await self.disconnect()
                except Dlt698Error as error:
                    # 对端已先关闭或取消完成时，资源仍已收尾；保持 aclose 的幂等约定。
                    if error.code not in (
                        n.ErrorCode.closed,
                        n.ErrorCode.not_associated,
                        n.ErrorCode.cancelled,
                    ):
                        raise
        finally:
            self.close()

    async def __aenter__(self) -> Self:
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
            exc.add_note(f"dlt698 async close failed: {cleanup_error}")
