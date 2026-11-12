"""通过原生异步会话读取，退出 async with 后资源已收尾。"""

import asyncio

from dlt698 import AsyncClient, Data, Oad, Server


async def main() -> None:
    attribute = Oad(oi=0x200F, attribute=2)
    with Server() as server:
        server.set(attribute, Data.uint16(5000))
        server.start_tcp("127.0.0.1", 0)
        async with AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            result = await client.get(attribute)
            print(result.require_data().as_uint16())


if __name__ == "__main__":
    asyncio.run(main())
