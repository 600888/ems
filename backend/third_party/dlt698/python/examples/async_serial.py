"""异步原生串口、候选点探测与确定性收尾；需要实际串口设备。"""

import argparse
import asyncio

from dlt698 import AsyncClient, Oad
from dlt698.transport import SerialLinkOptions, SerialOptions, SerialParity


async def main(path: str, baud: int) -> None:
    serial = SerialOptions(baud_rate=baud, data_bits=8, parity=SerialParity.even)
    async with AsyncClient() as client:
        await client.open_serial_configured(path, serial, SerialLinkOptions())
        for point in await client.probe_points([Oad(oi=0x200F, attribute=2)]):
            print(point.attribute, point.outcome, point.validation_error)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    parser.add_argument("--baud", type=int, default=9600)
    args = parser.parse_args()
    asyncio.run(main(args.path, args.baud))
