"""Exercise real daemon shutdown, resource cleanup and restart without external services."""

import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import uuid


def eventually(predicate, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError("condition timed out")


def signal_daemon(process, close_window=False):
    if os.name != "nt":
        process.send_signal(signal.SIGTERM)
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FreeConsole()
    if not kernel.AttachConsole(process.pid):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if close_window:
            kernel.GetConsoleWindow.restype = ctypes.c_void_p
            window = kernel.GetConsoleWindow()
            # The test driver must leave the target console before closing its window.
            kernel.FreeConsole()
            user = ctypes.WinDLL("user32", use_last_error=True)
            user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                         ctypes.c_size_t, ctypes.c_ssize_t]
            if not window or not user.PostMessageW(window, 0x10, 0, 0):
                raise ctypes.WinError(ctypes.get_last_error())
        elif not kernel.GenerateConsoleCtrlEvent(1, process.pid):
            raise ctypes.WinError(ctypes.get_last_error())
        time.sleep(0.1)
    finally:
        kernel.FreeConsole()


def worker(root, stubborn):
    stopping = False

    def stop(*_):
        nonlocal stopping
        if not stubborn:
            stopping = True

    for name in ("SIGINT", "SIGTERM", "SIGBREAK"):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), stop)
    (root / "worker.ready").write_text(str(os.getpid()))
    while not stopping:
        time.sleep(0.02)
    time.sleep(0.25)
    (root / "worker.stopped").write_text("stopped")


def run(daemon, rtdb, probe, scenario):
    with tempfile.TemporaryDirectory(prefix="daemon-lifecycle-", dir=Path.cwd()) as folder:
        root = Path(folder).resolve()
        (root / "conf").mkdir()
        (root / "conf/log.json").write_text(json.dumps({"modules": {}}))
        segment = "daemon_test_" + uuid.uuid4().hex
        realtime = root / "conf/realtime.json"
        realtime.write_text(json.dumps({"realtime": {"backend": {
            "type": "shm", "options": {"segment_name": segment, "segment_bytes": 1048576}
        }}}))
        env = {**os.environ, "EMS_ROOT": str(root)}
        env["PATH"] = str(rtdb.parent) + os.pathsep + env.get("PATH", "")

        def command(*args, expected=0):
            result = subprocess.run(args, env=env, capture_output=True, timeout=10)
            assert result.returncode == expected, (args, result.returncode, result.stdout,
                                                   result.stderr)
            return result

        if scenario == "abandoned":
            command(str(rtdb), "--config", str(realtime), "--init-only")
            command(str(probe), "--abandon-managed-lock", segment, expected=42)

        processes = [{
            "name": "test_rtdb_" + segment, "cmd": str(rtdb),
            "args": ["--config", str(realtime)],
            "cleanupArgs": ["--config", str(realtime), "--cleanup"],
            "runDir": str(root), "start": {"cnt": 1},
        }, {
            "name": "test_client_" + segment, "cmd": sys.executable,
            "args": [str(Path(__file__).resolve()), "--worker", str(root),
                     "stubborn" if scenario == "forced" else "normal"],
            "depends": ["test_rtdb_" + segment], "runDir": str(root),
            "start": {"cnt": 1},
        }]
        if scenario == "cleanup_failure":
            processes[0]["cleanupArgs"] = ["--config", str(root / "missing.json"), "--cleanup"]
        config = root / "conf/proc_manage.json"
        config.write_text(json.dumps({"stopTimeout": 1, "processes": processes}))
        output = root / "daemon.stdout"
        process = None
        try:
            cycles = 2 if scenario == "normal" else 1
            for cycle in range(cycles):
                for marker in (root / "worker.ready", root / "worker.stopped"):
                    marker.unlink(missing_ok=True)
                options = {}
                if os.name == "nt":
                    options["creationflags"] = subprocess.CREATE_NEW_CONSOLE
                    startup = subprocess.STARTUPINFO()
                    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    startup.wShowWindow = 0
                    options["startupinfo"] = startup
                with output.open("wb") as stream:
                    process = subprocess.Popen([str(daemon), "--config", str(config)],
                                               env=env, stdout=stream, stderr=stream, **options)
                    eventually(lambda: (root / "worker.ready").exists() or
                               process.poll() is not None)
                    assert process.poll() is None, output.read_text(errors="replace")
                    if scenario == "abandoned":
                        # Wait for initialization to fail before requesting daemon shutdown.
                        eventually(lambda: b"shared-memory initialization locked" in
                                   output.read_bytes())
                    else:
                        eventually(lambda: subprocess.run(
                            [str(probe), "--probe-managed-config", str(realtime)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0)
                    signal_daemon(process, scenario == "window_close")
                    expected = 1 if scenario == "cleanup_failure" else 0
                    assert process.wait(timeout=12) == expected, output.read_text(errors="replace")
                if scenario == "cleanup_failure":
                    command(str(probe), "--probe-managed-config", str(realtime))
                else:
                    command(str(probe), "--check-managed-absent", segment)
                if scenario not in ("forced", "window_close"):
                    assert (root / "worker.stopped").exists(), "client was not stopped gracefully"
                logs = (root / "log/ems-daemon/ems-daemon.log").read_text(encoding="utf-8")
                client_exit = logs.rfind("进程 test_client_" + segment + " 已按要求退出")
                cleanup_start = logs.rfind("清理进程 test_rtdb_" + segment)
                assert 0 <= client_exit < cleanup_start, logs
            print("PASS", scenario)
        except BaseException:
            print(output.read_text(errors="replace"), file=sys.stderr)
            raise
        finally:
            if process is not None and process.poll() is None:
                signal_daemon(process)
                process.wait(timeout=12)
            command(str(rtdb), "--config", str(realtime), "--cleanup")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--worker":
        worker(Path(sys.argv[2]), sys.argv[3] == "stubborn")
    else:
        run(*(Path(value).resolve() for value in sys.argv[1:4]), sys.argv[4])
