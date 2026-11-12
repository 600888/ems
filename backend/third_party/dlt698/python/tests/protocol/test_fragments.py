"""离线链路工具和 GET 分块收集的独立边界。"""

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.protocol.apdu import GetBlockTransfer
from dlt698.protocol.link import (
    Fragment,
    FragmentType,
    LinkFragmenter,
    LinkReassembler,
    decode_fragment,
    encode_fragment,
)


def test_fragments_roundtrip_duplicates_order_and_limit():
    payload = b"abcdefghij"
    sender = LinkFragmenter(payload, 4, 10)
    receiver = LinkReassembler(10)
    first = decode_fragment(encode_fragment(sender.current()))
    accepted = receiver.accept(first)
    assert accepted.acknowledge == 0 and accepted.apdu is None
    assert receiver.active and receiver.accept(first).duplicate
    with pytest.raises(d.Dlt698Error):
        receiver.accept(Fragment(type=FragmentType.middle, sequence=2, data=b"x"))
    sender.acknowledge(0)
    assert receiver.accept(sender.current()).acknowledge == 1
    sender.acknowledge(1)
    last = sender.current()
    assert last.type == FragmentType.last
    assert receiver.accept(last).apdu == payload
    assert receiver.accept(last).duplicate
    assert not receiver.active
    receiver.reset()
    with pytest.raises(d.Dlt698Error):
        receiver.accept(last)
    with pytest.raises(d.Dlt698Error):
        encode_fragment(Fragment(type=FragmentType.acknowledgement, data=b"x"))
    limited = LinkReassembler(5)
    limited.accept(first)
    with pytest.raises(d.Dlt698Error):
        limited.accept(Fragment(type=FragmentType.last, sequence=1, data=b"xy"))


def test_get_blocks_order_budget_and_remote_dar():
    attributes = [
        n.AttributeResult(attribute=d.Oad(oi=0xF001, attribute=2, index=i), result=d.Data.uint32(i))
        for i in range(8)
    ]
    response = n.GetResponse(piid_acd=1, list=True, attributes=attributes)
    blocks = GetBlockTransfer.split(response, 32)
    assert len(blocks) > 1
    collector = GetBlockTransfer(1, False)
    for block in blocks[:-1]:
        assert collector.accept(block) is None
    result = collector.accept(blocks[-1])
    assert [item.result.as_uint32() for item in result.attributes] == list(range(8))
    with pytest.raises(d.Dlt698Error):
        collector.accept(blocks[-1])
    with pytest.raises(d.Dlt698Error):
        GetBlockTransfer(1, False).accept(blocks[1])
    with pytest.raises(d.Dlt698Error) as failure:
        GetBlockTransfer(1, False).accept(n.GetNextResponse(piid_acd=1, last=True, result=4))
    assert failure.value.remote_code == 4
