"""生成和核对源码身份；sdist 无 Git 时继续验证相同源码。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def source_digest(root: Path) -> str:
    """按路径和内容散列实际内核/绑定输入，忽略缓存及生成物。"""
    paths = [
        root / p
        for p in (
            "VERSION",
            "pyproject.toml",
            "CMakeLists.txt",
            ".clang-format",
            "python/release-matrix.json",
        )
    ]
    for directory in (
        "cpp",
        "cmake",
        "python/bindings",
        "python/src",
        "python/tools",
        "python/tests",
        "python/examples",
    ):
        paths.extend(p for p in (root / directory).rglob("*") if p.is_file())
    for directory in ("third/asio/include", "third/pybind11/include", "third/pybind11/tools"):
        paths.extend(p for p in (root / directory).rglob("*") if p.is_file())
    paths.extend(
        root / p
        for p in ("python/CMakeLists.txt", "python/api-map.json", "third/pybind11/CMakeLists.txt")
    )
    digest = hashlib.sha256()
    for path in sorted(set(paths), key=lambda p: p.relative_to(root).as_posix()):
        if "__pycache__" in path.parts or path.suffix == ".pyc":
            continue
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        # Git Windows 换行转换不能改变发布身份。
        digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
        digest.update(b"\0")
    return digest.hexdigest()


def information(root: Path) -> dict[str, str]:
    """返回经源码摘要核对的构建身份；正式构建要求干净提交。"""
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
        raise ValueError("VERSION must be a single X.Y.Z")
    digest = source_digest(root)
    archived = root / "_source.json"
    if archived.exists():
        info: dict[str, str] = json.loads(archived.read_text(encoding="utf-8"))
        if info["version"] != version or info["source_sha256"] != digest:
            raise ValueError("sdist source/version differs from its provenance")
    else:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        expected = os.environ.get("DLT698_RELEASE_COMMIT")
        if expected:
            if expected != commit:
                raise ValueError("Release commit differs from checkout")
            dirty = subprocess.check_output(
                ["git", "status", "--porcelain", "--untracked-files=all"], cwd=root, text=True
            )
            if dirty.strip():
                raise ValueError("Formal release requires a clean checkout")
        info = {
            "version": version,
            "commit": commit,
            "source_sha256": digest,
            "mode": "release" if expected else "development",
        }
    info["api_sha256"] = hashlib.sha256(
        (root / "python/api-map.json").read_bytes().replace(b"\r\n", b"\n")
    ).hexdigest()
    return info


def main() -> None:
    """将身份写到构建树，源码树不生成版本文件。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    info = information(args.root.resolve())
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "_build_info.json").write_text(
        json.dumps(info, indent=2) + "\n", encoding="utf-8"
    )
    values = "\n".join(f"#define DLT698_PY_{k.upper()} {json.dumps(v)}" for k, v in info.items())
    (args.output / "build_info.hpp").write_text("#pragma once\n" + values + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
