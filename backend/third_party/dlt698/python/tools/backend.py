"""PEP 517 后端：复用 scikit-build-core，向 sdist 附加可核对身份。"""

from __future__ import annotations

import io
import json
import os
import tarfile
from pathlib import Path

from provenance import information
from scikit_build_core import build as _build

build_wheel = _build.build_wheel
build_editable = _build.build_editable
get_requires_for_build_wheel = _build.get_requires_for_build_wheel
get_requires_for_build_sdist = _build.get_requires_for_build_sdist
get_requires_for_build_editable = _build.get_requires_for_build_editable
prepare_metadata_for_build_wheel = _build.prepare_metadata_for_build_wheel
prepare_metadata_for_build_editable = _build.prepare_metadata_for_build_editable


def build_sdist(sdist_directory: str, config_settings: dict[str, object] | None = None) -> str:
    """只修改生成的源码包，在无 Git 环境也保留同一版本/提交身份。"""
    info = information(Path.cwd())
    name = _build.build_sdist(sdist_directory, config_settings)
    path = Path(sdist_directory) / name
    temporary = path.with_suffix(".tmp")
    with tarfile.open(path, "r:gz") as source, tarfile.open(temporary, "w:gz") as target:
        members = source.getmembers()
        prefix = members[0].name.split("/")[0]
        for member in members:
            relative = member.name.removeprefix(prefix + "/")
            allowed = relative in {
                "VERSION",
                "LICENSE",
                "README.md",
                "pyproject.toml",
                "PKG-INFO",
                "CMakeLists.txt",
                ".clang-format",
            }
            allowed |= any(
                relative.startswith(p)
                for p in (
                    "cpp/",
                    "cmake/",
                    "python/",
                    "tests/vectors/",
                    "third/asio/include/",
                    "third/pybind11/include/",
                    "third/pybind11/tools/",
                    "third/Catch2/",
                )
            )
            allowed |= relative in {
                "third/asio/LICENSE_1_0.txt",
                "third/pybind11/CMakeLists.txt",
                "third/pybind11/LICENSE",
            }
            if allowed and "__pycache__" not in member.name and not relative.endswith(".pyc"):
                target.addfile(member, source.extractfile(member) if member.isfile() else None)
        data = (json.dumps(info, sort_keys=True, indent=2) + "\n").encode()
        member = tarfile.TarInfo(prefix + "/_source.json")
        member.size = len(data)
        member.mode = 0o644
        member.mtime = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
        target.addfile(member, io.BytesIO(data))
    temporary.replace(path)
    return name
