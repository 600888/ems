"""同一 C++ 服务端的 asyncio 生命周期与多客户端访问。"""

import asyncio

from dlt698 import AsyncClient, AsyncServer, Data, Oad


async def main() -> None:
    attribute = Oad(oi=0x200F, attribute=2)
    async with AsyncServer() as server:
        server.set(attribute, Data.uint16(5000))
        await server.start_tcp("127.0.0.1", 0)
        async with AsyncClient() as client:
            await client.connect_tcp("127.0.0.1", server.local_port)
            print((await client.get(attribute)).require_data().as_uint16())


if __name__ == "__main__":
    asyncio.run(main())
