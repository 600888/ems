"""Verify Windows kills managed children and detached descendants when daemon exits."""

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import uuid

from lifecycle_test import eventually, signal_daemon


def worker(root, descendant=False):
    # A detached grandchild cannot receive the daemon console's close/break events.
    for name in ("SIGINT", "SIGTERM", "SIGBREAK"):
        signal.signal(getattr(signal, name), signal.SIG_IGN)
    if not descendant:
        subprocess.Popen(
            [sys.executable, str(Path(__file__).resolve()), "--descendant", str(root)],
            creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    marker = "descendant.pid" if descendant else "worker.pid"
    (root / marker).write_text(str(os.getpid()))
    while True:
        time.sleep(0.05)


def run(daemon, scenario):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handles = []
    process = None
    with tempfile.TemporaryDirectory(prefix="daemon-parent-exit-", dir=Path.cwd()) as folder:
        root = Path(folder).resolve()
        (root / "conf").mkdir()
        (root / "conf/log.json").write_text(json.dumps({"modules": {}}))
        processes = []
        for single_instance in (True, False):
            work = root / str(single_instance)
            work.mkdir()
            processes.append({
                "name": "test_parent_exit_" + uuid.uuid4().hex,
                "cmd": sys.executable,
                "args": [str(Path(__file__).resolve()), "--worker", str(work)],
                "singleInstance": single_instance, "start": {"cnt": 1},
            })
        config = root / "conf/proc_manage.json"
        # Keep the shutdown budget longer than the usual OS console-close deadline.
        config.write_text(json.dumps({"stopTimeout": 15, "processes": processes}))
        output = root / "daemon.stdout"
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        try:
            with output.open("wb") as stream:
                process = subprocess.Popen(
                    [str(daemon), "--config", str(config)],
                    env={**os.environ, "EMS_ROOT": str(root)},
                    stdout=stream, stderr=stream,
                    creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup,
                )
                for single_instance in (True, False):
                    for marker in ("worker.pid", "descendant.pid"):
                        path = root / str(single_instance) / marker
                        eventually(lambda: path.exists() or process.poll() is not None)
                        assert process.poll() is None, output.read_text(errors="replace")
                        eventually(lambda: path.stat().st_size > 0)
                        pid = int(path.read_text())
                        # Retain handles before stopping daemon so PID reuse cannot mask a leak.
                        handle = kernel.OpenProcess(0x100000 | 0x1, False, pid)
                        if not handle:
                            raise ctypes.WinError(ctypes.get_last_error())
                        handles.append(handle)
                if scenario == "killed":
                    process.kill()
                else:
                    signal_daemon(process, close_window=True)
                process.wait(timeout=20)
                for handle in handles:
                    assert kernel.WaitForSingleObject(handle, 5000) == 0, (
                        "managed child or detached descendant survived daemon exit")
            print("PASS", scenario)
        except BaseException:
            print(output.read_text(errors="replace"), file=sys.stderr)
            raise
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            for handle in handles:
                # Also clean up a failing run against the old implementation.
                if kernel.WaitForSingleObject(handle, 0) == 258:
                    kernel.TerminateProcess(handle, 1)
                    kernel.WaitForSingleObject(handle, 5000)
                kernel.CloseHandle(handle)


if __name__ == "__main__":
    if sys.argv[1] in ("--worker", "--descendant"):
        worker(Path(sys.argv[2]), sys.argv[1] == "--descendant")
    else:
        run(Path(sys.argv[1]).resolve(), sys.argv[2])
