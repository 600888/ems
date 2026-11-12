"""复用仓库独立规范向量，并检查字段和异常边界。"""

from pathlib import Path

import pytest

from dlt698 import DataType
from dlt698 import _native as n

VECTORS = Path(__file__).resolve().parents[3] / "tests/vectors"


@pytest.mark.parametrize("path", sorted(VECTORS.glob("*.hex")), ids=lambda p: p.name)
def test_shared_vectors(path):
    raw = bytes.fromhex(path.read_text().strip())
    if path.name == "get-frame.hex":
        frame = n.decode_frame(raw)
        assert n.encode_frame(frame) == raw
    else:
        message = n.decode_apdu(raw)
        assert n.encode_apdu(message) == raw


def test_known_response_fields():
    raw = bytes.fromhex((VECTORS / "get-list-response.hex").read_text().strip())
    response = n.decode_apdu(raw)
    assert isinstance(response, n.GetResponse)
    assert response.piid_acd == 2
    assert response.attributes[0].result.type == DataType.array
    assert [value.as_uint16() for value in response.attributes[0].result.value] == [2413] * 3


def test_crc_and_stream_owning_events():
    assert n.crc16(b"123456789") == 0x906E
    raw = bytes.fromhex((VECTORS / "get-frame.hex").read_text().strip())
    decoder = n.FrameStreamDecoder()
    assert decoder.feed(raw[:5]) == []
    events = decoder.feed(raw[5:] + raw)
    assert len(events) == 2
    assert all(isinstance(event, n.Frame) for event in events)
