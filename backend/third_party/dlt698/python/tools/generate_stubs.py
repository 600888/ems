"""从已安装扩展核对签名，再补 Python 输入协议与显式字段构造器。"""

from __future__ import annotations

import argparse
import ast
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def generate() -> str:
    output = ROOT / "build/python-stubs"
    subprocess.run(
        [
            sys.executable,
            "-m",
            "pybind11_stubgen",
            "dlt698._native",
            "-o",
            str(output),
            "--ignore-invalid-expressions",
            r"^<dlt698[.]_native[.][^>]+ object at 0x[0-9A-Fa-f]+>$",
            "--enum-class-locations",
            "ConnectionProfile:dlt698._native.ConnectionProfile",
            "--enum-class-locations",
            "Role:dlt698._native.Role",
            "--exit-code",
        ],
        check=True,
    )
    raw = (output / "dlt698/_native.pyi").read_text(encoding="utf-8")
    raw = raw.replace("dlt698._native.", "").replace("typing.Any", "object")
    raw = raw.replace("ConnectionProfile.ConnectionProfile.", "ConnectionProfile.")
    raw = raw.replace("Role.Role.", "Role.")
    raw = re.sub(r"\blist\[", "builtins.list[", raw)
    tree = ast.parse(raw)
    tree.body.insert(2, ast.Import(names=[ast.alias(name="builtins")]))
    session_class = next(
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "SessionHandle"
    )
    exchange = next(
        method
        for method in session_class.body
        if isinstance(method, ast.FunctionDef) and method.name == "async_exchange"
    )
    apdu_type = ast.unparse(
        next(arg.annotation for arg in exchange.args.args if arg.arg == "request")
    )
    for node in tree.body:
        if not isinstance(node, ast.ClassDef):
            continue
        properties = {}
        writable = {
            method.name
            for method in node.body
            if isinstance(method, ast.FunctionDef)
            and any(
                isinstance(d, ast.Attribute) and d.attr == "setter" for d in method.decorator_list
            )
        }
        for method in node.body:
            if isinstance(method, ast.FunctionDef) and any(
                isinstance(d, ast.Name) and d.id == "property" for d in method.decorator_list
            ):
                properties[method.name] = method.returns
        for name in (
            "security_backend",
            "security_backend_factory",
            "proxy",
            "calendar_clock",
            "set_transmit",
            "async_drain",
        ):
            if name in properties:
                nullable = ast.BinOp(
                    left=properties[name], op=ast.BitOr(), right=ast.Constant(value=None)
                )
                properties[name] = nullable
                for method in node.body:
                    if isinstance(method, ast.FunctionDef) and method.name == name:
                        if any(
                            isinstance(d, ast.Name) and d.id == "property"
                            for d in method.decorator_list
                        ):
                            method.returns = nullable
        for method in node.body:
            if not isinstance(method, ast.FunctionDef):
                continue
            callback_results = {
                "IChannel": {"async_read": "bytes", "async_write": "None"},
                "TcpChannel": {"connect": "None"},
                "TcpListener": {"async_accept": "TcpChannel"},
                "SessionHandle": {
                    "async_connect": "ConnectResponse",
                    "async_link": "LinkResponse",
                    "async_release": "None",
                    "async_get": "GetResponse",
                    "async_set": "SetResponse",
                    "async_action": "ActionResponse",
                    "async_get_record": "GetRecordResponse",
                    "async_exchange": "Apdu",
                },
            }
            outcome = callback_results.get(node.name, {}).get(method.name)
            if outcome is not None:
                if outcome == "Apdu":
                    # 交换消息已有原生 variant 的精确类型签名，直接沿用 request 注解。
                    outcome = ast.unparse(
                        next(arg.annotation for arg in method.args.args if arg.arg == "request")
                    )
                for arg in method.args.args:
                    if arg.arg == "callback":
                        arg.annotation = ast.parse(
                            f"collections.abc.Callable[[{outcome} | Error], None]", mode="eval"
                        ).body
            if node.name == "SessionHandle" and method.name in {
                "set_state_handler",
                "set_close_handler",
                "set_traffic_handler",
                "set_request_handler",
                "set_record_handler",
                "set_set_handler",
                "set_action_handler",
                "set_report_handler",
                "set_follow_handler",
                "set_diagnostic_handler",
                "set_acd_handler",
            }:
                method.args.args[-1].annotation = ast.BinOp(
                    left=method.args.args[-1].annotation,
                    op=ast.BitOr(),
                    right=ast.Constant(value=None),
                )
            if node.name == "SessionHandle" and method.name == "set_advanced_handler":
                method.args.args[-1].annotation = ast.parse(
                    f"collections.abc.Callable[[{apdu_type}, "
                    f"collections.abc.Callable[[{apdu_type} | Error], None]], "
                    "collections.abc.Callable[[], None] | None] | None",
                    mode="eval",
                ).body
            if node.name == "ProxyProvider" and method.name == "async_request":
                method.args.args[-1].annotation = ast.parse(
                    f"collections.abc.Callable[[{apdu_type} | Error], None]", mode="eval"
                ).body
                method.returns = ast.parse(
                    "collections.abc.Callable[[], None] | None", mode="eval"
                ).body
            if method.name == "__eq__" and node.name in {"Data", "Oad", "Omd"}:
                method.args.args[-1].annotation = ast.Name(id="object", ctx=ast.Load())
            if method.name == "__init__" and method.args.kwarg is not None:
                method.args.kwarg = None
                method.args.kwonlyargs = [
                    ast.arg(arg=k, annotation=v) for k, v in properties.items() if k in writable
                ]
                method.args.kw_defaults = [ast.Constant(value=Ellipsis) for _ in writable]
            if any(
                isinstance(d, ast.Attribute) and d.attr == "setter" for d in method.decorator_list
            ):
                method.args.args[-1].annotation = properties[method.name]
            if node.name == "Data":
                scalars = {
                    "boolean": "bool",
                    "float32": "float",
                    "float64": "float",
                    "visible_string": "str",
                    "utf8_string": "str",
                    "date_time": "collections.abc.Sequence[int]",
                    "date_time_s": "collections.abc.Sequence[int]",
                    "date": "collections.abc.Sequence[int]",
                    "time": "collections.abc.Sequence[int]",
                    **dict.fromkeys(
                        ("octet_string", "tsa", "mac", "rn"), "bytes | bytearray | memoryview"
                    ),
                    **dict.fromkeys(
                        (
                            "int8",
                            "int16",
                            "int32",
                            "int64",
                            "uint8",
                            "uint16",
                            "uint32",
                            "uint64",
                            "oi",
                            "enumeration",
                        ),
                        "int",
                    ),
                }
                if method.name in scalars:
                    method.args.args[0].annotation = ast.parse(
                        scalars[method.name], mode="eval"
                    ).body
                if method.name == "value":
                    union = (
                        "None | bool | int | float | str | bytes | builtins.list[Data] | "
                        "builtins.list[int] | Oad | Omd | Ti | ScalerUnit | Sid | SidMac | "
                        "Comdcb | BitString | Road | Region | SelectAll | Selector1 | Selector2 | "
                        "Selector3 | Selector4 | Selector5 | Selector6 | Selector7 | Selector8 | "
                        "Selector9 | Selector10 | NoMeters | AllMeters | MeterTypes | "
                        "MeterAddresses | MeterNumbers | MeterTypeRegions | "
                        "MeterAddressRegions | MeterNumberRegions | "
                        "builtins.list[Oad | Road]"
                    )
                    method.returns = ast.parse(union, mode="eval").body
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "build_info":
            node.returns = ast.parse("dict[str, str]", mode="eval").body
        if isinstance(node, ast.FunctionDef) and node.name in {
            "find_object",
            "find_attribute",
            "find_record",
        }:
            node.returns = ast.BinOp(
                left=node.returns, op=ast.BitOr(), right=ast.Constant(value=None)
            )
        if isinstance(node, ast.FunctionDef) and node.name in {
            "crc16",
            "decode_apdu",
            "decode_data",
            "decode_frame",
            "decode_security",
            "decode_fragment",
        }:
            node.args.args[0].annotation = ast.parse(
                "bytes | bytearray | memoryview", mode="eval"
            ).body
        if isinstance(node, ast.FunctionDef) and node.name == "async_probe_points":
            node.args.args[-1].annotation = ast.parse(
                "collections.abc.Callable[[builtins.list[PointResult] | Error], None]", mode="eval"
            ).body
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef):
            # 角色枚举按名称排在 Engine 后；stub 的默认值不可引用尚未声明的类。
            node.args.defaults = [
                ast.Constant(value=Ellipsis)
                if isinstance(default, ast.Attribute)
                and isinstance(default.value, ast.Name)
                and default.value.id == "Role"
                else default
                for default in node.args.defaults
            ]
            args = node.args.posonlyargs + node.args.args
            for arg, default in zip(
                args[len(args) - len(node.args.defaults) :], node.args.defaults, strict=True
            ):
                if isinstance(default, ast.Constant) and default.value is None:
                    arg.annotation = ast.BinOp(left=arg.annotation, op=ast.BitOr(), right=default)
    ast.fix_missing_locations(tree)
    text = ast.unparse(tree) + "\n"
    text = text.replace(
        "__hash__: typing.ClassVar[None] = None",
        "__hash__: typing.ClassVar[None] = None  # type: ignore[assignment]",
    )
    lint = subprocess.run(
        [
            sys.executable,
            "-m",
            "ruff",
            "check",
            "--fix",
            "--ignore",
            "E501",
            "--stdin-filename",
            "_native.pyi",
        ],
        input=text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if lint.returncode:
        raise RuntimeError(lint.stderr)
    text = lint.stdout
    process = subprocess.run(
        [sys.executable, "-m", "ruff", "format", "--stdin-filename", "_native.pyi"],
        input=text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if process.returncode:
        raise RuntimeError(process.stderr)
    return process.stdout


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = ROOT / "python/src/dlt698/_native.pyi"
    text = generate()
    if args.check:
        if path.read_text(encoding="utf-8") != text:
            raise SystemExit("Native signatures/stubs differ; regenerate and review typed API")
    else:
        path.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
