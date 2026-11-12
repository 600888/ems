"""安全认证扩展点。"""

from .._native import AuthenticationResult
from .backend import SecurityBackend

__all__ = ["AuthenticationResult", "SecurityBackend"]
