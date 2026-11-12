"""托管同步客户端：复用 C++ 关联、分块与取消语义。"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass

from .. import _native as n
from ..common.errors import DarError
from ._lifecycle import Lifecycle


@dataclass(frozen=True, slots=True)
class ReadResult:
    """精确 Data 或原始 DAR，两种结果必须且只能存在一种。"""

    data: n.Data | None = None
    dar: int | None = None

    def __post_init__(self) -> None:
        if (self.data is None) == (self.dar is None):
            raise ValueError("ReadResult must contain exactly one of data and dar")
        if self.data is not None and not isinstance(self.data, n.Data):
            raise TypeError("data must be Data")
        if self.dar is not None and (type(self.dar) is not int or not 0 <= self.dar <= 255):
            raise ValueError("DAR must be an integer in 0..255")

    def require_data(self) -> n.Data:
        """显式要求读取成功；业务拒绝抛带原码的 DarError。"""
        if self.data is None:
            assert self.dar is not None
            raise DarError(self.dar)
        return self.data


def _read_result(value: n.Data | int) -> ReadResult:
    return ReadResult(data=value) if isinstance(value, n.Data) else ReadResult(dar=value)


class Client(Lifecycle):
    """拥有一条连接与工作线程；使用 with 或 close() 确定性收尾。

    不自动重连，不自动重试 SET/ACTION。同步等待释放 GIL，另一业务线程可以取消。
    on_event 仅由显式 dispatch_events() 调用，原生线程只复制事件到有界队列。
    """

    def __init__(
        self,
        options: n.ClientOptions | None = None,
        *,
        on_event: Callable[[n.Event], None] | None = None,
    ):
        super().__init__(on_event)
        self._native = n.NativeClient(options or n.ClientOptions(), self._events)

    def connect_tcp(
        self, host: str, port: int, profile: n.ConnectionProfile = n.ConnectionProfile.remote_public
    ) -> None:
        """等待 DNS/TCP、必要 LINK 和 CONNECT；成功表示已经关联。"""
        self._native.connect_tcp(host, port, profile)

    def open_serial(
        self,
        path: str,
        baud: int = 9600,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """默认 8E1 并建立关联；设备打开耗时沿用 C++ 限制。"""
        self._native.open_serial(path, baud, profile)

    def get(self, attribute: n.Oad) -> ReadResult:
        """读取完整属性或一级元素；保留原始 DAR，自动收齐原生分块。"""
        return _read_result(self._native.get(attribute))

    def open_serial_configured(
        self,
        path: str,
        serial: n.SerialOptions,
        link: n.SerialLinkOptions,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """按实际线路参数打开串口；适用于自动方向适配器，真实时序须现场验证。"""
        self._native.open_serial_configured(path, serial, link, profile)

    def get_list(self, attributes: Sequence[n.Oad]) -> n.GetResponse:
        """非空列表，返回完整响应与逐项 Data/DAR，不改变顺序或部分成功。"""
        return self._native.get_list(list(attributes))

    def get_record(self, record: n.GetRecord) -> n.RecordResult:
        """返回拥有型完整记录快照或 DAR，保留实际列描述符。"""
        return self._native.get_record(record)

    def get_record_list(self, records: Sequence[n.GetRecord]) -> n.GetRecordResponse:
        """按查询顺序返回记录及逐项失败，自动完成原生分块。"""
        return self._native.get_record_list(list(records))

    def set(self, attribute: n.Oad, value: n.Data) -> int:
        """远端协议 SET，返回原始 DAR；本地成功不等于远端 DAR 为零。"""
        return self._native.set(attribute, value)

    def set_list(self, attributes: Sequence[n.SetAttribute]) -> n.SetResponse:
        """逐项写入，保留部分成功，不回滚、不重试。"""
        return self._native.set_list(list(attributes))

    def action(self, method: n.Omd, parameter: n.Data) -> n.ActionValue:
        """执行远端方法，返回 DAR 与可选 Data；取消后远端执行结果可能未知。"""
        return self._native.action(method, parameter)

    def action_list(self, methods: Sequence[n.ActionMethod]) -> n.ActionResponse:
        """执行非空方法列表并保留逐项结果。"""
        return self._native.action_list(list(methods))

    def request_disconnect(self) -> None:
        """非阻塞地取消连接/请求，不发送 RELEASE，可由另一业务线程调用。"""
        self._native.request_disconnect()

    def disconnect(self) -> None:
        """幂等关闭并等待原生 I/O 与线程收尾，之后可以重新连接。"""
        self._native.disconnect()

    def close(self) -> None:
        """停止 Python 事件分发，再确定性关闭原生连接。"""
        self._disable_events()
        self.disconnect()

    @property
    def state(self) -> n.ClientState:
        """最近发布的原生连接状态；connected 表示协议关联完成。"""
        return self._native.state
