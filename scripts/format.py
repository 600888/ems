#!/usr/bin/env python3
"""Format owned C/C++, CMake and web sources using the repository rules."""

import argparse
import difflib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
LOCAL_TOOLS = ROOT / ".cache/format-tools"
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp"}
WEB_SUFFIXES = {".vue", ".ts", ".tsx", ".js", ".jsx", ".css", ".scss", ".html"}
EXCLUDED = {"third_party", "node_modules", "build", "doc_build", ".git", ".cache", ".rspress"}


def owned_files(directory):
    """Walk owned directories without traversing dependencies, outputs or symlinks."""
    for parent, directories, files in os.walk(ROOT / directory, followlinks=False):
        directories[:] = [name for name in directories
                          if name not in EXCLUDED and not (Path(parent) / name).is_symlink()]
        for name in files:
            path = Path(parent) / name
            if not path.is_symlink():
                yield path.relative_to(ROOT)


def collect():
    groups = {"cpp": [], "cmake": [], "web": []}
    for directory in ("backend/include", "backend/src", "backend/tests", "backend/examples"):
        groups["cpp"].extend(path for path in owned_files(directory) if path.suffix in CPP_SUFFIXES)

    groups["cmake"] = [path for path in owned_files("backend")
                       if path.name == "CMakeLists.txt" or path.suffix == ".cmake"]
    # This one file belongs to EMS and only wires up vendored projects.
    groups["cmake"].append(Path("backend/third_party/CMakeLists.txt"))

    for directory in ("frontend/src", "website/theme"):
        groups["web"].extend(path for path in owned_files(directory) if path.suffix in WEB_SUFFIXES)
    groups["web"].extend(map(Path, ("frontend/vite.config.ts", "frontend/index.html",
                                    "website/rspress.config.ts")))
    return {key: sorted(set(paths)) for key, paths in groups.items()}


def commands(scope, env):
    if scope == "cpp":
        executable = os.environ.get("CLANG_FORMAT")
        if not executable:
            local = LOCAL_TOOLS / "clang_format/data/bin" / (
                "clang-format.exe" if os.name == "nt" else "clang-format")
            executable = str(local) if local.is_file() else shutil.which("clang-format")
        if not executable:
            raise RuntimeError("Install tools: python -m pip install -r scripts/requirements-format.txt")
        version = subprocess.check_output([executable, "--version"], env=env, text=True)
        if not re.search(r"\b22\.1\.8\b", version):
            raise RuntimeError("clang-format 22.1.8 is required; set CLANG_FORMAT to that executable")
        return [executable, "--style=file"], ["-i"], ["--dry-run", "--Werror"]

    if scope == "cmake":
        base = [sys.executable, "-m", "cmakelang.format", "-c", str(ROOT / ".cmake-format.yaml")]
        subprocess.run([sys.executable, "-c", "import cmakelang"], env=env, check=True)
        return base, ["-i", "--"], ["--check", "--"]

    node = shutil.which("node")
    prettier = ROOT / "frontend/node_modules/prettier/bin/prettier.cjs"
    if not node or not prettier.is_file():
        raise RuntimeError("Install web tools: npm --prefix frontend ci")
    base = [node, str(prettier), "--config", str(ROOT / ".prettierrc.json"),
            "--ignore-path", str(ROOT / ".prettierignore")]
    return base, ["--write"], ["--check"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="check without modifying files")
    mode.add_argument("--diff", action="store_true", help="preview changes without modifying files")
    parser.add_argument("--scope", choices=("all", "cpp", "cmake", "web"), default="all")
    parser.add_argument("paths", nargs="*", help="optional owned files/directories, relative to repository")
    args = parser.parse_args()

    groups = collect()
    if args.paths:
        selected = []
        for name in args.paths:
            path = (ROOT / name).resolve()
            if not path.is_relative_to(ROOT) or not path.exists():
                parser.error(f"path is missing or outside the repository: {name}")
            selected.append(path)
        groups = {key: [path for path in files
                        if any((ROOT / path).is_relative_to(item) for item in selected)]
                  for key, files in groups.items()}

    env = os.environ.copy()
    if LOCAL_TOOLS.is_dir():
        env["PYTHONPATH"] = str(LOCAL_TOOLS) + os.pathsep + env.get("PYTHONPATH", "")

    scopes = [key for key in groups if args.scope in ("all", key) and groups[key]]
    if not scopes:
        print("No owned source files selected; dependencies and build outputs are excluded.")
        return 0

    # Validate all tools before formatting the first file.
    tools = {scope: commands(scope, env) for scope in scopes}
    failed = False
    for scope in scopes:
        files = groups[scope]
        base, write, check = tools[scope]
        print(f"{scope}: {len(files)} files", flush=True)
        if args.diff:
            for path in files:
                result = subprocess.run(base + (["--"] if scope == "cmake" else []) + [str(path)],
                                        cwd=ROOT, env=env,
                                        capture_output=True, text=True, encoding="utf-8")
                if result.returncode:
                    print(result.stderr, file=sys.stderr)
                    failed = True
                    continue
                before = (ROOT / path).read_text(encoding="utf-8")
                sys.stdout.writelines(difflib.unified_diff(
                    before.splitlines(keepends=True), result.stdout.splitlines(keepends=True),
                    fromfile=f"a/{path.as_posix()}", tofile=f"b/{path.as_posix()}"))
        else:
            # Bounded argument batches also work with Windows command line limits.
            for start in range(0, len(files), 20):
                result = subprocess.run(base + (check if args.check else write) +
                                        [str(path) for path in files[start:start + 20]],
                                        cwd=ROOT, env=env)
                failed = failed or result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Formatting failed: {error}", file=sys.stderr)
        sys.exit(1)
