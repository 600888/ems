"""只允许同版本、同提交、同源码与完整冻结矩阵的联合制品公开。"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from artifact_check import inspect
from provenance import information

ROOT = Path(__file__).resolve().parents[2]


def cpp_record(directory: Path) -> None:
    info = information(ROOT)
    for archive in sorted(directory.iterdir()):
        if not archive.name.endswith((".zip", ".tar.gz")):
            continue
        value = {
            **info,
            "filename": archive.name,
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        }
        (directory / (archive.name + ".provenance.json")).write_text(
            json.dumps(value, indent=2) + "\n", encoding="utf-8"
        )


def combine(
    directory: Path, version: str, commit: str, *, python_only: bool = False
) -> dict[str, object]:
    config = json.loads((ROOT / "python/release-matrix.json").read_text())
    # 精简发布只要求 Python 制品，清单同步标明不包含 C++ 库矩阵。
    if python_only:
        config["linkages"] = []
    cpp_expected = {
        f"dlt698-{version}-{p['platform']}-{linkage}"
        + (".zip" if p["platform"].startswith("windows") else ".tar.gz")
        for p in config["platforms"]
        for linkage in config["linkages"]
    }
    wheel_expected = {
        ("cp" + minor, p["wheel_platform"])
        for p in config["platforms"]
        for minor in config["python"]
    }
    rows = []
    actual_cpp = set()
    actual_wheels = set()
    sdists = []
    for path in sorted(directory.iterdir()):
        if path.name.endswith(".provenance.json"):
            row = json.loads(path.read_text())
            archive = directory / row["filename"]
            if hashlib.sha256(archive.read_bytes()).hexdigest() != row["sha256"]:
                raise ValueError("C++ archive differs from its recorded hash")
            actual_cpp.add(archive.name)
            rows.append(row)
        elif path.suffix == ".whl":
            rows.append(inspect(path))
            parts = path.stem.split("-")
            if parts[0] != "dlt698" or parts[1] != version or parts[-2] != parts[-3]:
                raise ValueError("unexpected wheel version/ABI")
            # auditwheel 可以附加兼容 tag；冻结基线必须真实存在于 tag 集。
            compatible = [tag for tag in parts[-1].split(".") if (parts[-3], tag) in wheel_expected]
            if len(compatible) != 1:
                raise ValueError("wheel outside the frozen support matrix")
            pair = (parts[-3], compatible[0])
            if pair in actual_wheels:
                raise ValueError("duplicate platform/interpreter wheel")
            actual_wheels.add(pair)
        elif path.name == f"dlt698-{version}.tar.gz":
            sdists.append(inspect(path))
            rows.extend(sdists[-1:])
    if actual_cpp != cpp_expected or actual_wheels != wheel_expected or len(sdists) != 1:
        raise ValueError("joint release incomplete: C++/wheel/sdist matrix differs")
    expected_files = {row["filename"] for row in rows}
    archive_files = {
        p.name for p in directory.iterdir() if p.name.endswith((".whl", ".tar.gz", ".zip"))
    }
    if expected_files != archive_files:
        raise ValueError("unexpected archive in release directory")
    identities = {
        (r["version"], r["commit"], r["source_sha256"], r["api_sha256"], r["mode"]) for r in rows
    }
    if len(identities) != 1 or next(iter(identities))[:2] != (version, commit):
        raise ValueError("joint release has mixed source/version/commit identities")
    if next(iter(identities))[-1] != "release":
        raise ValueError("development artifacts cannot be formally published")
    return {
        "schema": 1,
        "version": version,
        "commit": commit,
        "source_sha256": sdists[0]["source_sha256"],
        "api_sha256": sdists[0]["api_sha256"],
        "matrix": config,
        "artifacts": rows,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--cpp-record", action="store_true")
    parser.add_argument("--python-only", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--commit")
    args = parser.parse_args()
    if args.cpp_record:
        cpp_record(args.directory)
        return
    manifest = combine(args.directory, args.version, args.commit, python_only=args.python_only)
    (args.directory / "release-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    checksums = "".join(f"{row['sha256']}  {row['filename']}\n" for row in manifest["artifacts"])
    manifest_hash = hashlib.sha256(
        (args.directory / "release-manifest.json").read_bytes()
    ).hexdigest()
    (args.directory / "SHA256SUMS").write_text(
        checksums + f"{manifest_hash}  release-manifest.json\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
