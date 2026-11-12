"""同一 C++ 内核的有类型接口；导入只验证身份，不启动线程或连接。"""

from __future__ import annotations

import importlib
import json
import sys
import sysconfig
from importlib.metadata import version as _installed_version
from pathlib import Path
from types import ModuleType

if sys.implementation.name != "cpython" or not (3, 11) <= sys.version_info[:2] < (3, 15):
    raise ImportError("dlt698 requires CPython 3.11 through 3.14")
if sysconfig.get_config_var("Py_GIL_DISABLED"):
    raise ImportError("dlt698 does not yet support the free-threaded ABI")
_interpreters: ModuleType | None
try:
    _interpreters = importlib.import_module("_xxsubinterpreters")
except ImportError:
    try:
        _interpreters = importlib.import_module("_interpreters")
    except ImportError:
        _interpreters = None
if _interpreters is not None and _interpreters.get_current() != _interpreters.get_main():
    raise ImportError("dlt698 requires the main interpreter")

from . import _native  # noqa: E402

_info = _native.build_info()
# editable 安装的 Python 包装来自源码树；身份记录与实际加载的扩展一起安装。
_packaged_info = json.loads(
    Path(_native.__file__).with_name("_build_info.json").read_text(encoding="utf-8")
)
if _info != _packaged_info or _installed_version("dlt698") != _info["version"]:
    raise ImportError(
        "dlt698 metadata/wrapper/native build identity mismatch; reinstall the package"
    )

__version__: str = _info["version"]
build_info = _native.build_info

from ._native import (  # noqa: E402
    ClientOptions,
    ClientState,
    ConnectionProfile,
    Data,
    DataType,
    Device,
    DeviceLayout,
    DeviceOptions,
    ErrorCode,
    Event,
    EventQueue,
    Limits,
    Oad,
    Omd,
    ServerOptions,
    ServerState,
    SessionOptions,
    Wiring,
)
from .app import AsyncClient, AsyncServer, Client, ReadResult, Server  # noqa: E402
from .codec import decode_data, encode_data  # noqa: E402
from .errors import DarError, Dlt698Error  # noqa: E402

__all__ = [
    "AsyncClient",
    "AsyncServer",
    "Client",
    "ClientOptions",
    "ClientState",
    "ConnectionProfile",
    "Data",
    "DataType",
    "Device",
    "DeviceLayout",
    "DeviceOptions",
    "ErrorCode",
    "Event",
    "EventQueue",
    "Limits",
    "Oad",
    "Omd",
    "ReadResult",
    "Server",
    "ServerOptions",
    "ServerState",
    "SessionOptions",
    "Wiring",
    "DarError",
    "Dlt698Error",
    "build_info",
    "decode_data",
    "encode_data",
]
