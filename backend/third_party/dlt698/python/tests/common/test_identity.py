"""版本混装拒绝、完整错误字段与主解释器约束。"""

import importlib.metadata
import json
import subprocess
import sys
from pathlib import Path

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.errors import Dlt698Error, _from_native


def test_identity_matches_installed_distribution():
    assert d.__version__ == importlib.metadata.version("dlt698")
    assert d.build_info() == json.loads(Path(n.__file__).with_name("_build_info.json").read_text())
    assert len(d.build_info()["source_sha256"]) == 64
    assert len(d.build_info()["api_sha256"]) == 64


@pytest.mark.parametrize("code", list(d.ErrorCode.__members__.values()))
def test_every_error_code_preserves_all_fields(code):
    error = _from_native(code, 17, "错误上下文", 7)
    assert isinstance(error, Dlt698Error)
    assert (error.code, error.offset, error.context, error.remote_code) == (
        code,
        17,
        "错误上下文",
        7,
    )


def test_metadata_mismatch_fails_before_use():
    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import importlib.metadata; importlib.metadata.version=lambda name:'99.0.0'; "
            "import dlt698",
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert completed.returncode != 0
    assert "identity mismatch" in completed.stderr


def test_subinterpreter_is_rejected_without_hanging():
    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import importlib; "
            "x=importlib.import_module('_interpreters' if "
            "__import__('sys').version_info[:2]>=(3,13) else '_xxsubinterpreters'); "
            "i=x.create(); result=x.run_string(i, 'import dlt698'); "
            "assert result is None, str(result)",
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert completed.returncode != 0
    assert "main interpreter" in completed.stderr
