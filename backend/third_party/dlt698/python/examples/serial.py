"""读取自动方向串口适配器上的频率；实际端口、接线与时序需现场确认。"""

import argparse

from dlt698 import Client, Oad
from dlt698.expert import SerialLinkOptions, SerialOptions


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="例如 COM3 或 /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=9600)
    args = parser.parse_args()
    with Client() as client:
        serial = SerialOptions(baud_rate=args.baud)
        link = SerialLinkOptions(baud_rate=args.baud, bits_per_character=11)
        client.open_serial_configured(args.port, serial, link)
        print(client.get(Oad(oi=0x200F, attribute=2)).require_data().as_uint16())


if __name__ == "__main__":
    main()
