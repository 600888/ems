"""能力未知/否定、读取回退以及独立记录验证。"""

import pytest

import dlt698 as d
from dlt698 import _native as n
from dlt698 import standard as s


def test_capability_plan_preserves_order_duplicates_and_negative_hints():
    energy = d.Oad(oi=0x0010, attribute=2, index=1)
    unknown = d.Oad(oi=0xF001, attribute=2)
    attributes = [energy, unknown, energy]
    capabilities = s.Capabilities()
    assert s.service_support(capabilities, s.ReadService.list) == s.Support.unknown
    assert [batch.attributes for batch in s.plan_reads(capabilities, attributes)] == [
        [energy],
        [unknown],
        [energy],
    ]
    parameters = n.AssociationParameters(protocol=[0xE0] + [0] * 7)
    capabilities = s.capabilities_from_connect(n.ConnectResponse(parameters=parameters))
    batches = s.plan_reads(capabilities, attributes, batch_size=2)
    assert [batch.attributes for batch in batches] == [[energy, unknown], [energy]]
    assert all(batch.list for batch in batches)
    parameters.protocol = [0xC0] + [0] * 7
    parameters.function = [0x40] + [0] * 15  # 明确提供业务信息，但未置有功电能位。
    capabilities.negotiated = parameters
    assert s.point_support(capabilities, energy) == s.Support.no
    assert [item.attribute for item in s.candidate_points(capabilities, attributes, True)] == [
        unknown
    ]
    assert len(s.plan_reads(capabilities, attributes)) == 3
    with pytest.raises(d.Dlt698Error) as failure:
        s.require_record_service(capabilities)
    assert failure.value.code == d.ErrorCode.unsupported_service
    s.require_record_service(s.Capabilities())
    with pytest.raises(d.Dlt698Error):
        s.plan_reads(capabilities, attributes, batch_size=0)
    with pytest.raises(d.Dlt698Error) as failure:
        s.capabilities_from_connect(n.ConnectResponse(result=3))
    assert failure.value.remote_code == 3


def test_record_validation_preserves_diagnostics_and_owning_results():
    valid = n.DateTimeS(value=[7, 234, 10, 7, 0, 0, 0])
    s.validate_record_time(valid)
    with pytest.raises(d.Dlt698Error):
        s.validate_record_time(n.DateTimeS(value=[7, 234, 2, 30, 0, 0, 0]))
    columns = [d.Oad(oi=0x2023, attribute=2), d.Oad(oi=0x2021, attribute=2)]
    query = s.make_record_query(0x5004, n.SelectAll(), columns)
    s.validate_record_query(query)
    row = [d.Data.uint32(1), d.Data.date_time_s(valid.value)]
    result = n.RecordResult(attribute=query.attribute, columns=columns, result=[row])
    s.validate_record_result(query, result)
    s.validate_record_cell(0x5004, columns[0], row[0])
    with pytest.raises(d.Dlt698Error):
        s.validate_record_cell(0x5004, columns[0], d.Data.uint16(1))
    result.result = [[row[0]]]
    with pytest.raises(d.Dlt698Error):
        s.validate_record_result(query, result)
