"""接口防漂移门槛：显式映射、头文件契约摘要和运行时导出检查。"""

from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import re
from pathlib import Path

from generate_values import GROUPS

ROOT = Path(__file__).resolve().parents[2]


def headers() -> dict[str, str]:
    """记录所有公开头文件的有效内容，包括成员、枚举、默认值及内联实现。"""
    result = {}
    for path in sorted((ROOT / "cpp/include").rglob("*.hpp")):
        text = path.read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        text = " ".join(text.split())
        result[path.relative_to(ROOT).as_posix()] = hashlib.sha256(text.encode()).hexdigest()
    return result


def mappings() -> list[dict[str, object]]:
    """绑定成员逐项映射；其他公开表面仍有显式整头文件待开放记录。"""
    entries: list[dict[str, object]] = []
    for group, (namespace, header, classes) in GROUPS.items():
        for name, fields in classes.items():
            short = name.split("::")[-1]
            symbol = (namespace + "::" if namespace else "") + name
            for field in ["", *fields.split()]:
                entries.append(
                    {
                        "cpp": "dlt698::" + symbol + ("::" + field if field else ""),
                        "header": "cpp/include/dlt698/" + header,
                        "python": "dlt698._native." + short + ("." + field if field else ""),
                        "status": "exposed",
                        "phase": group,
                        "contract": "owning native value; native defaults preserved",
                        "tests": [
                            "python/tests/model/test_values.py",
                            "python/tests/app/test_application.py",
                        ],
                    }
                )
    for name in ("Client", "Server", "Device"):
        native = "Native" + name if name != "Device" else name
        methods = {
            "Client": (
                "connect_tcp open_serial get get_list get_record get_record_list set set_list "
                "action action_list request_disconnect disconnect state"
            ),
            "Server": (
                "set device start_tcp start_serial request_stop stop state local_port connections"
            ),
            "Device": "set set_element get define",
        }[name].split()
        for method in methods:
            entries.append(
                {
                    "cpp": f"dlt698::{'service' if name == 'Device' else 'app'}::{name}::{method}",
                    "python": f"dlt698._native.{native}.{method}",
                    "status": "exposed",
                    "phase": "P1" if method in ("get", "set", "connect_tcp", "start_tcp") else "P2",
                    "contract": "same core; Result failures translated, DAR preserved",
                    "tests": ["python/tests/app/test_application.py"],
                }
            )
    for header in headers():
        entries.append(
            {
                "header": header,
                "status": "inventory",
                "phase": "all",
                "contract": (
                    "signatures/defaults/enums tracked by normalized header digest; "
                    "unmapped members are internal or deferred"
                ),
            }
        )
    for method in (
        "begin_connect",
        "accept_connect",
        "verify_connect",
        "protect_request",
        "open_request",
        "protect_response",
        "open_response",
        "reset",
    ):
        entries.append(
            {
                "cpp": "dlt698::security::IBackend::" + method,
                "header": "cpp/include/dlt698/security/backend.hpp",
                "python": "dlt698.expert.SecurityBackend." + method,
                "status": "exposed",
                "phase": "P3",
                "contract": "caller-driven trampoline; preserve Result errors; no fake ESAM",
                "tests": ["python/tests/security/test_security.py"],
            }
        )
    entries.append(
        {
            "cpp": "dlt698::service::AdvancedServiceOptions::trans",
            "header": "cpp/include/dlt698/service/advanced.hpp",
            "python": "dlt698._native.TransBridge",
            "status": "exposed",
            "phase": "P3",
            "contract": "Engine transparent constructor bridges nonblocking native completion",
            "tests": ["python/tests/transport/test_transparent.py"],
        }
    )
    entries.append(
        {
            "cpp": "dlt698::session::validate_options",
            "header": "cpp/include/dlt698/session/session.hpp",
            "python": "dlt698._native.validate_session_options",
            "status": "exposed",
            "phase": "P3",
            "contract": "native preflight; no backend factory calls or reset side effects",
            "tests": ["python/tests/security/test_security.py"],
        }
    )
    return entries


def exported_surface(symbols: list[dict[str, object]]) -> list[dict[str, object]]:
    """更新时盘点已实际注册的同名表面；CI 只核对冻结结果，不自动开放新接口。"""
    native = importlib.import_module("dlt698._native")
    aliases = {
        "Enum": "Enumeration",
        "IObjectProvider": "ObjectProvider",
        "IProxyProvider": "ProxyProvider",
        "IBackend": "SecurityBackend",
        "Client": "NativeClient",
        "Server": "NativeServer",
        "State": "SessionState",
        "Session": "SessionHandle",
    }
    entries = []
    for symbol in symbols:
        cpp = str(symbol["cpp"])
        parts = cpp.split("::")
        name = aliases.get(parts[-1], parts[-1])
        target = None
        if symbol["kind"] in {
            "CLASS_DECL",
            "STRUCT_DECL",
            "TYPE_ALIAS_DECL",
            "ENUM_DECL",
            "FUNCTION_DECL",
        } and hasattr(native, name):
            target = name
        if symbol["kind"] in {"FIELD_DECL", "CXX_METHOD", "ENUM_CONSTANT_DECL"}:
            parent = aliases.get(parts[-2], parts[-2])
            if hasattr(native, parent) and hasattr(getattr(native, parent), name):
                target = parent + "." + name
        if target is not None:
            entries.append(
                {
                    "cpp": cpp,
                    "header": symbol["header"],
                    "python": "dlt698._native." + target,
                    "status": "exposed",
                    "phase": "P2-P3",
                    "contract": "native owning value/Result adapter",
                    "tests": [
                        "python/tests/model/test_values.py",
                        "python/tests/service/test_expert.py",
                    ],
                }
            )
    return entries


def check_runtime(entries: list[dict[str, object]]) -> None:
    """验证映射实际存在，不把清单状态当作已经绑定的证据。"""
    for entry in entries:
        path = entry.get("python")
        if not isinstance(path, str):
            continue
        prefix = "dlt698.expert." if path.startswith("dlt698.expert.") else "dlt698._native."
        value = importlib.import_module(prefix.removesuffix("."))
        for part in path.removeprefix(prefix).split("."):
            value = getattr(value, part)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--runtime", action="store_true")
    args = parser.parse_args()
    path = ROOT / "python/api-map.json"
    if args.update:
        previous = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        entries = exported_surface(previous.get("symbols", [])) + mappings()
        deduplicated = {str(item.get("cpp", item.get("header"))): item for item in entries}
        path.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "headers": headers(),
                    "entries": list(deduplicated.values()),
                    "symbols": previous.get("symbols", []),
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    data = json.loads(path.read_text(encoding="utf-8"))
    if data["headers"] != headers():
        raise SystemExit(
            "C++ header contract changed: update bindings, map, stubs and tests in this change"
        )
    if args.runtime:
        check_runtime(data["entries"])
    print(f"API contract checked: {len(data['headers'])} headers, {len(data['entries'])} entries")


if __name__ == "__main__":
    main()
