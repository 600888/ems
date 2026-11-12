"""最小托管 TCP 读取与服务器本地更新。"""

from dlt698 import Client, Data, Oad, Server


def main() -> None:
    attribute = Oad(oi=0x200F, attribute=2)
    with Server() as server:
        server.set(attribute, Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        with Client() as client:
            client.connect_tcp("127.0.0.1", server.local_port)
            print(client.get(attribute).require_data().as_uint16())
            server.set(attribute, Data.uint16(4998))
            print(client.get(attribute).require_data().as_uint16())


if __name__ == "__main__":
    main()
