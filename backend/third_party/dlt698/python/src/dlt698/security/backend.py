"""调用线程驱动的安全后端扩展点。"""

from .. import _native as n


class SecurityBackend(n.SecurityBackend):
    """认证/安全后端扩展点，错误用 Dlt698Error；绝不自动宣告认证成功。

    只允许用于显式驱动 Engine/AsyncClient 的 SessionOptions，回调在驱动线程执行。
    业务回调必须有限耗时；SDK、密钥和真实 ESAM 由应用提供。
    多连接监听使用 security_backend_factory，为每个 Session 创建独立实例。
    """

    def __init__(self) -> None:
        required = (
            "begin_connect",
            "accept_connect",
            "verify_connect",
            "protect_request",
            "open_request",
            "protect_response",
            "open_response",
            "reset",
        )
        missing = [
            name
            for name in required
            if not callable(getattr(type(self), name, None))
            or getattr(type(self), name) is getattr(SecurityBackend, name)
        ]
        if missing:
            raise TypeError("SecurityBackend must implement: " + ", ".join(missing))
        super().__init__()

    def begin_connect(
        self,
    ) -> n.NullSecurity | n.PasswordSecurity | n.SymmetrySecurity | n.SignatureSecurity:
        raise NotImplementedError

    def accept_connect(self, request: n.ConnectRequest) -> n.AuthenticationResult:
        raise NotImplementedError

    def verify_connect(self, request: n.ConnectRequest, response: n.ConnectResponse) -> None:
        raise NotImplementedError

    def protect_request(self, application: bytes) -> n.SecurityRequest:
        raise NotImplementedError

    def open_request(self, message: n.SecurityRequest) -> bytes:
        raise NotImplementedError

    def protect_response(self, application: bytes) -> n.SecurityResponse:
        raise NotImplementedError

    def open_response(self, message: n.SecurityResponse) -> bytes:
        raise NotImplementedError

    def reset(self) -> None:
        """幂等清理后端状态；原生 noexcept，应用实现不得抛异常。"""
        raise NotImplementedError
