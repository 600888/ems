"""从显式值清单生成分层公开导出，避免通配导出暴露内部实现。"""

import re
from pathlib import Path

from generate_values import GROUPS

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    groups: dict[str, list[str]] = {
        "model": [
            "Data",
            "DataType",
            "Oad",
            "Omd",
            "Ti",
            "ScalerUnit",
            "BitString",
            "Sid",
            "SidMac",
            "Comdcb",
            "Boolean",
            "Int8",
            "Int16",
            "Int32",
            "Int64",
            "UInt8",
            "UInt16",
            "UInt32",
            "UInt64",
            "Enumeration",
            "Float32",
            "Float64",
            "OctetString",
            "VisibleString",
            "Utf8String",
            "DateTime",
            "DateTimeS",
            "Date",
            "Time",
            "Oi",
            "Tsa",
            "Mac",
            "Rn",
        ],
        "protocol": [
            "LinkRequestType",
            "GetBlockTransfer",
            "advanced_capability",
            "advanced_matches",
            "ProxyGetTarget",
            "ProxyGetResultTarget",
            "ProxySetTarget",
            "ProxySetResultTarget",
            "ProxyActionTarget",
            "ProxyActionResultTarget",
            "ProxySetThenGetTarget",
            "ProxySetThenGetResultTarget",
            "ProxyActionThenGetTarget",
            "ProxyActionThenGetResultTarget",
        ],
        "standard": [
            "Wiring",
            "Phase",
            "ArrayLayout",
            "DeviceLayout",
            "ValueDefinition",
            "AttributeDefinition",
            "ObjectDefinition",
            "ScaledNumber",
            "DemandValue",
            "RecordDefinition",
            "objects",
            "find_object",
            "find_attribute",
            "find_record",
            "validate_layout",
            "make_oad",
            "phase_oad",
            "tariff_oad",
            "harmonic_oad",
            "validate_value",
            "validate_oad",
            "energy_oad",
            "demand_oad",
            "demand_values",
            "engineering_values",
            "decimal_text",
            "approximate_value",
            "unit_symbol",
            "make_record_query",
            "record_at",
            "record_between",
            "record_sequences",
            "Support",
            "ReadService",
            "Capabilities",
            "ReadBatch",
            "CandidatePoint",
            "capabilities_from_connect",
            "service_support",
            "point_support",
            "candidate_points",
            "plan_reads",
            "require_record_service",
            "validate_record_time",
            "validate_record_query",
            "validate_record_cell",
            "validate_record_result",
        ],
    }
    groups["model"].extend(GROUPS["records"][2])
    for group in ("messages", "connection", "mutation", "advanced"):
        groups["protocol"].extend(GROUPS[group][2])
    for module, names in groups.items():
        names = sorted(set(names))
        # 导入按不区分大小写的自然顺序排列，与项目 Ruff 规则保持一致。
        imports = sorted(
            names,
            key=lambda value: (
                not value[0].isupper(),
                re.sub(r"\d+", lambda match: match.group().zfill(20), value.casefold()),
            ),
        )
        text = f'"""{module} 分层公开接口；直接使用原生拥有型值与实现。"""\n\n'
        text += (
            "from .._native import (\n" + "".join(f"    {name},\n" for name in imports) + ")\n\n"
        )
        text += "__all__ = [\n" + "".join(f'    "{name}",\n' for name in names) + "]\n"
        (
            ROOT
            / "python/src/dlt698"
            / module
            / ("apdu.py" if module == "protocol" else "__init__.py")
        ).write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
