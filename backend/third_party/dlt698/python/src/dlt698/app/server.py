"""托管服务器与连接之外保存的共享 Device。"""

from __future__ import annotations

from collections.abc import Callable

from .. import _native as n
from ._lifecycle import Lifecycle


class Server(Lifecycle):
    """托管多连接服务器；本地 set 不授予远端 SET 权限。"""

    def __init__(
        self,
        device: n.Device | None = None,
        options: n.ServerOptions | None = None,
        *,
        on_event: Callable[[n.Event], None] | None = None,
    ):
        super().__init__(on_event)
        self._native = n.NativeServer(device, options or n.ServerOptions(), self._events)

    def set(self, attribute: n.Oad, value: n.Data) -> None:
        """发布或更新完整本地属性，失败保留旧值；数组必须符合设备布局。"""
        self._native.set(attribute, value)

    def start_tcp(
        self,
        address: str,
        port: int,
        profile: n.ConnectionProfile = n.ConnectionProfile.remote_public,
    ) -> None:
        """绑定数字地址并监听；port=0 自动分配，成功不表示已有关联客户端。"""
        self._native.start_tcp(address, port, profile)

    def start_serial(
        self,
        path: str,
        baud: int = 9600,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """使用默认 8E1 打开串口并安装原生时序适配。"""
        self._native.start_serial(path, baud, profile)

    def request_stop(self) -> None:
        """非阻塞停止接入及会话，随后可由业务线程 stop() 等待收尾。"""
        self._native.request_stop()

    def start_serial_configured(
        self,
        path: str,
        serial: n.SerialOptions,
        link: n.SerialLinkOptions,
        profile: n.ConnectionProfile = n.ConnectionProfile.local_public,
    ) -> None:
        """按实际端口和串行帧间隔配置启动；队列预算沿用原生适配器。"""
        self._native.start_serial_configured(path, serial, link, profile)

    def stop(self) -> None:
        """幂等停止并等待原生线程退出，设备数据保留，可再次启动。"""
        self._native.stop()

    def close(self) -> None:
        """禁用 Python 回调并确定性停止服务器。"""
        self._disable_events()
        self.stop()

    @property
    def device(self) -> n.Device:
        """共享拥有型设备，不公开内部可变目录。"""
        return self._native.device

    @property
    def state(self) -> n.ServerState:
        """入口运行状态，区别于每个客户端的协议关联状态。"""
        return self._native.state

    @property
    def local_port(self) -> int:
        """当前 TCP 实际端口；停止或串口模式返回零。"""
        return self._native.local_port

    @property
    def connections(self) -> int:
        """原生最近发布的活动连接数量。"""
        return self._native.connections
