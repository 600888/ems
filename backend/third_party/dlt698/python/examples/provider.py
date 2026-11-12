"""在业务线程驱动原生服务，按实时业务值回答读取。"""

from dlt698 import Data, DataType, Oad
from dlt698.expert import AttributeSchema, Endpoint, ObjectProvider, ObjectRegistry, ObjectSchema


class Frequency(ObjectProvider):
    """示例 provider，回调必须及时返回，可以保存拥有型参数。"""

    def read(self, attribute: Oad) -> Data | int:
        return Data.uint16(5000)


def main() -> None:
    registry = ObjectRegistry()
    registry.register_object(
        ObjectSchema(oi=0x200F, attributes=[AttributeSchema(type=DataType.uint16)]), Frequency()
    )
    with Endpoint(objects=registry) as server:
        port = server.engine.listen("127.0.0.1", 0)
        print(f"127.0.0.1:{port}")
        try:
            while True:
                server.poll(0.01)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
