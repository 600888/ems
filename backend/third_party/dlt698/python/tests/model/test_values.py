"""独立规范预期值、精确标签、资源预算与 Python 边界。"""

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698.errors import DarError, Dlt698Error, ProtocolError, ResourceLimitError


@pytest.mark.parametrize(
    "factory,value,encoded",
    [
        ("null", None, "00"),
        ("boolean", True, "0301"),
        ("uint16", 5000, "121388"),
        ("int16", -2, "10fffe"),
        ("uint32", 0x12345678, "0612345678"),
        ("int32", -1, "05ffffffff"),
        ("uint64", 2**64 - 1, "15ffffffffffffffff"),
        ("octet_string", b"\x00\xff", "090200ff"),
        ("utf8_string", "中", "0c03e4b8ad"),
    ],
)
def test_independent_data_bytes(factory, value, encoded):
    data = getattr(d.Data, factory)() if value is None else getattr(d.Data, factory)(value)
    expected = bytes.fromhex(encoded)
    assert d.encode_data(data) == expected
    assert d.decode_data(expected) == data


@pytest.mark.parametrize(
    "factory,bad",
    [
        ("uint16", -1),
        ("uint16", 65536),
        ("uint64", 2**64),
        ("int8", 128),
    ],
)
def test_integer_overflow(factory, bad):
    with pytest.raises(OverflowError):
        getattr(d.Data, factory)(bad)


@pytest.mark.parametrize(
    "factory", ["int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"]
)
def test_bool_not_integer(factory):
    with pytest.raises(TypeError):
        getattr(d.Data, factory)(True)


def test_owning_and_recursive_values():
    source = bytearray(b"abc")
    data = d.Data.octet_string(source)
    source[0] = 0
    assert data.value == b"abc"
    array = d.Data.array([data, d.Data.uint16(1)])
    assert array != d.Data.structure(array.value)
    copy = array.value
    copy.clear()
    assert len(array.value) == 2
    with pytest.raises(TypeError):
        d.decode_data(memoryview(b"0123")[::2])


def test_errors_and_budgets():
    with pytest.raises(ProtocolError) as failure:
        d.decode_data(b"\x12\x01")
    assert failure.value.code == d.ErrorCode.need_more_data
    assert failure.value.offset >= 1
    with pytest.raises(Dlt698Error):
        d.decode_data(b"\x00\x00")
    with pytest.raises(ResourceLimitError):
        d.encode_data(d.Data.octet_string(b"abc"), d.Limits(max_data_bytes=2))
    with pytest.raises(TypeError):
        d.Data.uint16(1).as_int16()


def test_descriptors_and_record_data():
    oad = d.Oad(oi=0x200F, attribute=0xA2, index=3)
    assert d.encode_data(d.Data.oad(oad)) == bytes.fromhex("51200fa203")
    road = n.Road(attribute=oad, associated=[d.Oad(oi=1, attribute=2)])
    for value in [
        d.Data.road(road),
        d.Data.rsd(n.Selector9(previous=2)),
        d.Data.ms(n.AllMeters()),
        d.Data.rcsd([oad, road]),
        d.Data.region(n.Region(boundary=0, begin=d.Data.uint8(1), end=d.Data.uint8(3))),
    ]:
        assert d.decode_data(d.encode_data(value)) == value


def test_read_result_invariant():
    with pytest.raises(ValueError):
        d.ReadResult()
    with pytest.raises(ValueError):
        d.ReadResult(d.Data.null(), 3)
    with pytest.raises(DarError) as failure:
        d.ReadResult(dar=4).require_data()
    assert failure.value.dar == 4


def test_defaults_and_unknown_fields():
    assert d.ClientOptions().transport_timeout == 5.0
    assert d.ServerOptions().max_connections == 16
    with pytest.raises(TypeError):
        d.ClientOptions(typo=1)
    for timeout in [-1, float("nan"), float("inf"), 0.0001]:
        with pytest.raises(ValueError):
            d.ClientOptions(transport_timeout=timeout)
