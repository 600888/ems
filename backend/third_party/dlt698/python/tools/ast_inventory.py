"""Clang AST 公开成员清单：记录重载、字段、枚举和默认参数。"""

from __future__ import annotations

import argparse
import difflib
import json
import subprocess
import sys
from pathlib import Path

from clang import cindex

ROOT = Path(__file__).resolve().parents[2]


def declaration_signature(cursor: cindex.Cursor) -> str:
    """保留声明、默认参数与限定符，排除函数体及构造函数的成员初始化列表。"""
    kinds = cindex.CursorKind
    tokens = [
        token.spelling for token in cursor.get_tokens() if token.kind != cindex.TokenKind.COMMENT
    ]
    if cursor.kind in {
        kinds.CXX_METHOD,
        kinds.FUNCTION_DECL,
        kinds.FUNCTION_TEMPLATE,
        kinds.CONSTRUCTOR,
        kinds.DESTRUCTOR,
    }:
        # 模板构造函数在 libclang 中是 FUNCTION_TEMPLATE，类外定义也用语义父级识别。
        parent = cursor.semantic_parent
        constructor = cursor.kind == kinds.CONSTRUCTOR or (
            cursor.kind == kinds.FUNCTION_TEMPLATE
            and parent.kind in {kinds.STRUCT_DECL, kinds.CLASS_DECL, kinds.CLASS_TEMPLATE}
            and cursor.spelling == parent.spelling
        )
        depth = 0
        for i, token in enumerate(tokens):
            if token == "(":
                depth += 1
            elif token == ")":
                depth -= 1
            elif depth == 0 and (token == "{" or (constructor and token == ":")):
                # Linux 的 cursor 范围包含初始化列表，MSVC 目标可能提前结束；统一只留签名。
                tokens = tokens[:i]
                break
    else:
        tokens = tokens[: tokens.index("{")] if "{" in tokens else tokens
    return " ".join(tokens)


def inventory(includes: list[str]) -> list[dict[str, str]]:
    """只遍历项目公共声明，不遍历实现体或标准库 AST。"""
    include_root = ROOT / "cpp/include"
    source_path = ROOT / "build/python-api.cpp"
    source = "\n".join(
        f"#include <{path.relative_to(include_root).as_posix()}>"
        for path in sorted(include_root.rglob("*.hpp"))
    )
    args = ["-x", "c++", "-std=c++17", "-I" + str(include_root)]
    if sys.platform == "win32":
        args += ["--target=x86_64-pc-windows-msvc", "-fms-compatibility-version=19.39"]
    else:
        process = subprocess.run(
            ["c++", "-E", "-x", "c++", "-", "-v"],
            input="",
            capture_output=True,
            text=True,
            check=True,
        )
        block = process.stderr.split("#include <...> search starts here:")[1].split(
            "End of search list."
        )[0]
        includes += [line.strip() for line in block.splitlines() if line.strip().startswith("/")]
    args.extend("-I" + str(Path(p).resolve()) for p in includes)
    args.append("-DDLT698_STATIC")
    unit = cindex.Index.create().parse(
        str(source_path), args=args, unsaved_files=[(str(source_path), source)]
    )
    errors = [str(item) for item in unit.diagnostics if item.severity >= item.Error]
    if errors:
        raise RuntimeError("AST parse failed:\n" + "\n".join(errors))
    kinds = cindex.CursorKind
    declarations = {
        kinds.STRUCT_DECL,
        kinds.CLASS_DECL,
        kinds.CLASS_TEMPLATE,
        kinds.ENUM_DECL,
        kinds.ENUM_CONSTANT_DECL,
        kinds.FIELD_DECL,
        kinds.CXX_METHOD,
        kinds.FUNCTION_DECL,
        kinds.FUNCTION_TEMPLATE,
        kinds.CONSTRUCTOR,
        kinds.DESTRUCTOR,
        kinds.TYPE_ALIAS_DECL,
        kinds.TYPEDEF_DECL,
        kinds.VAR_DECL,
    }
    containers = {
        kinds.NAMESPACE,
        kinds.STRUCT_DECL,
        kinds.CLASS_DECL,
        kinds.CLASS_TEMPLATE,
        kinds.ENUM_DECL,
        kinds.UNEXPOSED_DECL,
    }
    result: dict[tuple[str, str, str], dict[str, str]] = {}

    def visit(cursor: cindex.Cursor, parents: list[str]) -> None:
        if cursor.access_specifier == cindex.AccessSpecifier.PRIVATE:
            return
        if cursor.location.file:
            path = Path(str(cursor.location.file)).resolve()
            if not path.is_relative_to(include_root):
                return
        name = cursor.spelling
        if cursor.kind in declarations and name:
            symbol = "::".join([*parents, name])
            # 内联实现和初始化行为由头文件摘要覆盖，不混入结构签名。
            signature = declaration_signature(cursor)
            header = Path(str(cursor.location.file)).resolve().relative_to(ROOT).as_posix()
            result[(symbol, cursor.kind.name, signature)] = {
                "cpp": symbol,
                "kind": cursor.kind.name,
                "signature": signature,
                "header": header,
            }
        if cursor.kind in containers:
            for child in cursor.get_children():
                visit(child, [*parents, name] if name else parents)

    for cursor in unit.cursor.get_children():
        if cursor.kind == kinds.NAMESPACE and cursor.spelling == "dlt698":
            visit(cursor, [])
    return sorted(result.values(), key=lambda item: (item["cpp"], item["kind"], item["signature"]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include", action="append", default=[])
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    symbols = inventory(args.include)
    path = ROOT / "python/api-map.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if args.update:
        actual_headers = {symbol["cpp"]: symbol["header"] for symbol in symbols}
        for entry in data["entries"]:
            if entry.get("cpp") in actual_headers:
                entry["header"] = actual_headers[entry["cpp"]]
        mapping = {item["cpp"]: item for item in data["entries"] if "cpp" in item}
        for symbol in symbols:
            target = mapping.get(symbol["cpp"])
            if target:
                symbol.update({k: v for k, v in target.items() if k not in symbol})
            else:
                internal = (
                    symbol["kind"] in {"CONSTRUCTOR", "DESTRUCTOR"} or "operator" in symbol["cpp"]
                )
                symbol["status"] = "internal" if internal else "deferred"
                symbol["reason"] = (
                    "Lifetime/operators are adapted by owning Python objects."
                    if internal
                    else "Low-level surface not exported; use the mapped application/Engine API."
                )
        data["symbols"] = symbols
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # 验证结构签名时忽略人工维护的映射/阶段说明。
    expected = [
        {key: item[key] for key in ("cpp", "kind", "signature", "header")}
        for item in data["symbols"]
    ]
    actual = [
        {key: item[key] for key in ("cpp", "kind", "signature", "header")} for item in symbols
    ]
    if expected != actual:
        print(
            "\n".join(
                difflib.unified_diff(
                    json.dumps(expected, ensure_ascii=False, indent=2).splitlines(),
                    json.dumps(actual, ensure_ascii=False, indent=2).splitlines(),
                    fromfile="reviewed python/api-map.json",
                    tofile="current public C++ AST",
                    lineterm="",
                )
            ),
            file=sys.stderr,
        )
        raise SystemExit("Public C++ AST differs: update binding contracts and reviewed API map")
    print(f"AST inventory checked: {len(symbols)} public declarations")


if __name__ == "__main__":
    main()
