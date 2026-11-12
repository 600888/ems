"""保留原生错误字段，业务 DAR 与本地失败分别处理。"""

from __future__ import annotations

from .._native import ErrorCode


class Dlt698Error(Exception):
    """本地失败，保留分类、字节偏移、上下文和可选远端 ERROR 原码。"""

    def __init__(
        self,
        code: ErrorCode,
        offset: int,
        context: str,
        remote_code: int | None = None,
        context_bytes: bytes | None = None,
    ):
        self.code = code
        self.offset = offset
        self.context = context
        self.remote_code = remote_code
        self.context_bytes = context.encode("utf-8") if context_bytes is None else context_bytes
        super().__init__(f"{code.name}: {context} (offset={offset}, remote_code={remote_code})")


class ProtocolError(Dlt698Error):
    """协议、校验或编解码失败。"""


class ResourceLimitError(Dlt698Error):
    """资源预算超限。"""


class TransportError(Dlt698Error):
    """通道关闭或 I/O 失败。"""


class RequestTimeoutError(Dlt698Error, TimeoutError):
    """请求超时，有副作用操作是否执行未知。"""


class CancelledError(Dlt698Error):
    """原生请求被取消。"""


class BusyError(Dlt698Error):
    """单在途事务或启停竞争拒绝本次操作。"""


class AssociationError(Dlt698Error):
    """尚未关联或 CONNECT 被拒绝。"""


class RemoteError(Dlt698Error):
    """远端 ERROR 响应，原码保存在 remote_code。"""


class DarError(Exception):
    """调用者显式要求成功数据时产生的业务 DAR 异常。"""

    def __init__(self, dar: int):
        self.dar = dar
        super().__init__(f"Remote DAR: {dar}")


def _from_native(
    code: ErrorCode,
    offset: int,
    context: str,
    remote_code: int | None,
    context_bytes: bytes | None = None,
) -> Dlt698Error:
    classes: dict[ErrorCode, type[Dlt698Error]] = {
        ErrorCode.resource_limit: ResourceLimitError,
        ErrorCode.closed: TransportError,
        ErrorCode.io_error: TransportError,
        ErrorCode.timeout: RequestTimeoutError,
        ErrorCode.cancelled: CancelledError,
        ErrorCode.busy: BusyError,
        ErrorCode.not_associated: AssociationError,
        ErrorCode.association_failed: AssociationError,
        ErrorCode.remote_error: RemoteError,
    }
    return classes.get(code, ProtocolError)(code, offset, context, remote_code, context_bytes)


__all__ = [
    "Dlt698Error",
    "ProtocolError",
    "ResourceLimitError",
    "TransportError",
    "RequestTimeoutError",
    "CancelledError",
    "BusyError",
    "AssociationError",
    "RemoteError",
    "DarError",
]
