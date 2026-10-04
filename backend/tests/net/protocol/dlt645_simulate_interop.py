"""Optional TCP interop check against ems_simulate's installed dlt645 package.

Run with ems_simulate's virtual-environment Python; no application is started.
All listeners bind to loopback, and package logs stay in this workspace.
"""

import argparse
import asyncio
import ctypes
from contextlib import ExitStack
from datetime import datetime
import importlib.util
import json
import os
from pathlib import Path
import socket
import sys
from types import ModuleType


METER = "123456789012"
ENERGY, DEMAND, VOLTAGE, EVENTS, PARAMETER = (
    0x00010000, 0x01010000, 0x02010100, 0x03030000, 0x04000105
)


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def unused_port():
    with socket.socket() as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        return endpoint.getsockname()[1]


def simulator_api(root, log_dir):
    # Load the simulator's actual demand-time compatibility wrapper without
    # importing application configuration or creating files in that checkout.
    config = ModuleType("src.config.global_config")
    config.LOG_DIR = str(log_dir)
    sys.modules[config.__name__] = config
    path = root / "src/device/protocol/dlt645_compat.py"
    spec = importlib.util.spec_from_file_location("ems_dlt645_interop_compat", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    from dlt645.model.types.dlt645_type import Demand
    return module.AsyncMeterClientService, module.AsyncMeterServerService, Demand


class Plugin:
    def __init__(self, library, config):
        self.library = library
        self.instance = library.ems_plugin_create()
        check(self.instance, "plugin create failed")
        try:
            self.call("init", json.dumps(config).encode())
            self.call("connect")
        except BaseException:
            self.close()
            raise

    def call(self, operation, *args):
        result = getattr(self.library, "ems_plugin_" + operation)(self.instance, *args)
        if result < 0:
            raise RuntimeError(self.library.ems_plugin_last_error(self.instance).decode())
        return result

    def read(self, expected):
        codes = (ctypes.c_char_p * 5)(*[f"0x{di:08X}".encode() for di in
                                      (ENERGY, DEMAND, VOLTAGE, PARAMETER, EVENTS)])
        output = ctypes.create_string_buffer(16384)
        check(self.call("read_batch", codes, 5, output, len(output)) == expected,
              "unexpected count of valid numeric results: " + output.value.decode())
        return json.loads(output.value)

    def close(self):
        if self.instance:
            self.library.ems_plugin_destroy(self.instance)
            self.instance = None


def load_plugin(path):
    library = ctypes.CDLL(str(path.resolve()))
    ptr = ctypes.c_void_p
    signatures = {
        "create": (ptr, []), "destroy": (None, [ptr]),
        "init": (ctypes.c_int, [ptr, ctypes.c_char_p]),
        "connect": (ctypes.c_int, [ptr]), "disconnect": (ctypes.c_int, [ptr]),
        "read_batch": (ctypes.c_int, [ptr, ctypes.POINTER(ctypes.c_char_p), ctypes.c_int,
                                      ctypes.POINTER(ctypes.c_char), ctypes.c_int]),
        "write": (ctypes.c_int, [ptr, ctypes.c_char_p, ctypes.c_double]),
        "last_error": (ctypes.c_char_p, [ptr]),
    }
    for name, (result, args) in signatures.items():
        function = getattr(library, "ems_plugin_" + name)
        function.restype, function.argtypes = result, args
    return library


async def verify(library, client_type, server_type, demand_type):
    # An asymmetric date exposes the upstream demand byte-order bug.
    occurred = datetime(2026, 7, 15, 21, 17)
    port = unused_port()
    config = {"ip": "127.0.0.1", "port": port, "meter_address": METER,
              "timeout_ms": 1000, "bcd_endian": "big", "password": "02123456"}
    server = server_type.new_tcp_server("127.0.0.1", port, timeout=2)
    server.set_address(METER)
    check(server.set_password("02123456"), "Python password setup failed")
    check(server.set_00(ENERGY, 123.45), "Python energy setup failed")
    check(server.set_01(DEMAND, demand_type(2.1234, occurred)), "Python demand setup failed")
    check(server.set_02(VOLTAGE, 220.1), "Python voltage setup failed")
    check(server.set_04(PARAMETER, "1234"), "Python parameter setup failed")
    check(server.set_03(EVENTS, [("000001", "000002"), ("000003", "000004"),
                                 ("000005", "000006")]), "Python event setup failed")
    check(await server.start(), "Python server start failed")
    plugin = None
    try:
        plugin = await asyncio.to_thread(Plugin, library, {**config, "allow_writes": True})
        result = await asyncio.to_thread(plugin.read, 4)
        check(abs(result[0]["value"] - 123.45) < 0.02, "C++ energy mismatch")
        check(abs(result[1]["value"] - 2.1234) < 0.0002, "C++ demand mismatch")
        check(result[1]["occur_time"] == int(occurred.timestamp() * 1000),
              "C++ demand occurrence time mismatch")
        check(abs(result[2]["value"] - 220.1) < 0.02, "C++ voltage mismatch")
        check(result[3]["value"] == 1234, "C++ parameter mismatch")
        check(result[4]["fields"][0]["value"] == "000001,000002", "C++ event mismatch")
        await asyncio.to_thread(plugin.call, "write", b"0x04000105", 2345)
        result = await asyncio.to_thread(plugin.read, 4)
        check(result[3]["value"] == 2345, "C++ to Python parameter write mismatch")
        print("C++ client -> ems_simulate Python server: PASS (00..04, demand time, write)")
    finally:
        if plugin:
            await asyncio.to_thread(plugin.close)
        await server.stop()

    port = unused_port()
    config.update(port=port, role="server_readonly", points=[
        {"di": ENERGY, "value": 123.45},
        {"di": DEMAND, "value": 2.1234, "occur_time": int(occurred.timestamp() * 1000)},
        {"di": VOLTAGE, "value": 220.1}, {"di": PARAMETER, "value": 1234},
        {"di": EVENTS, "fields": ["000001,000002", "000003,000004", "000005,000006"]},
    ])
    plugin = Plugin(library, config)
    client = client_type.new_tcp_client("127.0.0.1", port, timeout=1)
    client.set_address(METER)
    try:
        check(await client.connect(), "Python client connect failed")
        energy = await client.read_00(ENERGY)
        demand = await client.read_01(DEMAND)
        voltage = await client.read_02(VOLTAGE)
        parameter = await client.read_04(PARAMETER)
        events = await client.read_03(EVENTS)
        check(energy and abs(energy.value - 123.45) < 0.02, "Python energy mismatch")
        check(demand and abs(demand.value.value - 2.1234) < 0.0002, "Python demand mismatch")
        check(demand.value.time == occurred, "Python demand occurrence time mismatch")
        check(voltage and abs(voltage.value - 220.1) < 0.02, "Python voltage mismatch")
        check(parameter and int(parameter.value) == 1234, "Python parameter mismatch")
        check(events and len(events) == 3, "Python compound event mismatch")
        check(not await client.write_04(PARAMETER, "2345", "00000000"),
              "remote write unexpectedly accepted")
        check(int((await client.read_04(PARAMETER)).value) == 1234,
              "read-only server changed after remote write")
        plugin.call("write", b"0x02010100", 230.2)
        check(abs((await client.read_02(VOLTAGE)).value - 230.2) < 0.02,
              "Python did not observe local image update")
        print("ems_simulate Python client -> C++ server: PASS (00..04, demand time, read-only)")
    finally:
        await client.disconnect()
        plugin.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulate-root", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, action="append", default=[],
                        help="Windows compiler DLL directory, e.g. MinGW bin; repeatable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    api = simulator_api(args.simulate_root.resolve(), root / ".cache/dlt645-interop-logs")
    with ExitStack() as runtime_search:
        if os.name == "nt":
            for directory in args.runtime_dir:
                runtime_search.enter_context(os.add_dll_directory(str(directory.resolve())))
        asyncio.run(verify(load_plugin(args.plugin), *api))


if __name__ == "__main__":
    main()
