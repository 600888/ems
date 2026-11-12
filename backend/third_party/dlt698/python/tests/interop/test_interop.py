"""独立 C++ 安装包消费方与 Python 包双向互操作。"""

import os
import subprocess

import pytest

import dlt698 as d


@pytest.fixture
def consumer():
    path = os.environ.get("DLT698_INTEROP_EXE")
    if not path:
        pytest.skip("independent C++ consumer must be built separately; CI interop job runs this")
    return path


def test_python_client_cpp_server(consumer):
    process = subprocess.Popen(
        [consumer, "server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        assert process.stdout is not None
        port = int(process.stdout.readline())
        with d.Client() as client:
            client.connect_tcp("127.0.0.1", port)
            assert client.get(d.Oad(oi=0x200F, attribute=2)).require_data().as_uint16() == 5001
        output, error = process.communicate("stop\n", timeout=10)
        assert process.returncode == 0, (output, error)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


def test_cpp_client_python_server(consumer):
    with d.Server() as server:
        server.set(d.Oad(oi=0x200F, attribute=2), d.Data.uint16(4999))
        server.start_tcp("127.0.0.1", 0)
        result = subprocess.run(
            [consumer, "client", str(server.local_port)], capture_output=True, text=True, timeout=10
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout.strip() == "4999"
