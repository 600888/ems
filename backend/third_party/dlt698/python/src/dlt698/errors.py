"""Compatibility imports for dlt698.common.errors."""

from .common.errors import (
    AssociationError,
    BusyError,
    CancelledError,
    DarError,
    Dlt698Error,
    ProtocolError,
    RemoteError,
    RequestTimeoutError,
    ResourceLimitError,
    TransportError,
)
from .common.errors import _from_native as _from_native

__all__ = [
    "AssociationError",
    "BusyError",
    "CancelledError",
    "DarError",
    "Dlt698Error",
    "ProtocolError",
    "RemoteError",
    "RequestTimeoutError",
    "ResourceLimitError",
    "TransportError",
]
