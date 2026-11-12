"""检查实际 wheel/sdist 内容、构建身份、类型声明及许可证。"""

from __future__ import annotations

import argparse
import email
import hashlib
import json
import tarfile
import zipfile
from pathlib import Path


def inspect(path: Path) -> dict[str, str]:
    if path.suffix == ".whl":
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            metadata = email.message_from_bytes(
                archive.read(next(name for name in names if name.endswith(".dist-info/METADATA")))
            )
            info = json.loads(archive.read("dlt698/_build_info.json"))
            if metadata["Version"] != info["version"]:
                raise ValueError("wheel metadata/native provenance version mismatch")
            for name in (
                "dlt698/py.typed",
                "dlt698/_native.pyi",
                "dlt698/licenses/DLT698_LICENSE",
                "dlt698/licenses/PYBIND11_LICENSE",
                "dlt698/licenses/ASIO_LICENSE",
            ):
                if name not in names:
                    raise ValueError(f"wheel missing {name}")
            native = [
                name
                for name in names
                if name.startswith("dlt698/_native.") and name.endswith((".pyd", ".so"))
            ]
            if len(native) != 1:
                raise ValueError("wheel must have exactly one native extension")
            if any(name.endswith((".lib", ".hpp", ".cpp")) for name in names):
                raise ValueError("wheel includes unwanted SDK/source files")
    else:
        with tarfile.open(path) as archive:
            names = archive.getnames()
            prefix = names[0].split("/")[0] + "/"
            source = archive.extractfile(prefix + "_source.json")
            assert source is not None
            info = json.load(source)
            for name in names:
                relative = name.removeprefix(prefix)
                permitted = relative in {
                    "VERSION",
                    "LICENSE",
                    "README.md",
                    "pyproject.toml",
                    "CMakeLists.txt",
                    ".clang-format",
                    "PKG-INFO",
                    "_source.json",
                    "third/asio/LICENSE_1_0.txt",
                    "third/pybind11/LICENSE",
                    "third/pybind11/CMakeLists.txt",
                } or any(
                    relative.startswith(p)
                    for p in (
                        "cpp/",
                        "python/",
                        "cmake/",
                        "tests/vectors/",
                        "third/Catch2/",
                        "third/asio/include/",
                        "third/pybind11/include/",
                        "third/pybind11/tools/",
                    )
                )
                if not permitted or "__pycache__" in name or relative.endswith(".pyc"):
                    raise ValueError(f"sdist unexpected input: {name}")
    return {**info, "filename": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", type=Path, nargs="+")
    args = parser.parse_args()
    for path in args.paths:
        print(json.dumps(inspect(path), sort_keys=True))


if __name__ == "__main__":
    main()
