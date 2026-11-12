"""真实 TCP 记录分块与查询快照，不用语言内编解码回环代替。"""

import time

import dlt698 as d
from dlt698 import _native as n
from dlt698.expert import Engine, MemoryRecords, ObjectRegistry, register_standard_object


def test_record_selection_projection_and_native_blocks():
    columns = [
        d.Oad(oi=0x2023, attribute=2),
        d.Oad(oi=0x2021, attribute=2),
        d.Oad(oi=0x0010, attribute=2, index=1),
    ]
    records = MemoryRecords.create(0x5004, columns)
    records.replace_rows(
        [
            [
                d.Data.uint32(i),
                d.Data.date_time_s([7, 234, 10, i, 0, 0, 0]),
                d.Data.uint32(1000 + i),
            ]
            for i in range(1, 21)
        ]
    )
    registry = ObjectRegistry()
    register_standard_object(registry, 0x5004, b"\x02", records)
    parameters = n.AssociationParameters(apdu_bytes=128)
    # 通用会话原生协议位来自 SessionOptions 的完整默认值，不用空配置覆盖能力。
    default = d.SessionOptions().parameters
    parameters.protocol = default.protocol
    options = d.SessionOptions(parameters=parameters, prefer_get_blocks=True)
    server = Engine(options, registry)
    client = Engine(options)

    def wait(token):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            server.poll()
            for event in client.poll():
                if event.token == token:
                    assert event.error is None
                    return event
        raise AssertionError("record exchange did not complete")

    try:
        port = server.listen("127.0.0.1", 0, d.ConnectionProfile.local_preset)
        wait(client.connect_tcp("127.0.0.1", port, d.ConnectionProfile.local_preset))
        query = n.make_record_query(0x5004, n.SelectAll())
        result = wait(client.get_record([query])).message.records[0]
        assert result.columns == columns
        assert len(result.result) == 20
        assert result.result[-1][-1].as_uint32() == 1020
        query = n.record_sequences(0x5004, 3, 5, [columns[2], columns[0]])
        result = wait(client.get_record([query])).message.records[0]
        assert result.columns == [columns[2], columns[0]]
        assert [[v.as_uint32() for v in row] for row in result.result] == [
            [1003, 3],
            [1004, 4],
        ]
    finally:
        client.close()
        server.close()
