"""维护值对象绑定的显式清单；生成物必须入库并通过格式检查。"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 显式清单避免把头文件中新出现的内部类型自动变为稳定 Python API。
GROUPS = {
    "records": (
        "model",
        "model/record.hpp",
        {
            "Road": "attribute associated",
            "Region": "boundary begin end",
            "NoMeters": "",
            "AllMeters": "",
            "MeterTypes": "values",
            "MeterAddresses": "values",
            "MeterNumbers": "values",
            "MeterTypeRegions": "values",
            "MeterAddressRegions": "values",
            "MeterNumberRegions": "values",
            "SelectAll": "",
            "Selector1": "attribute value",
            "Selector2": "attribute begin end interval",
            "Selector3": "ranges",
            "Selector4": "time meters",
            "Selector5": "time meters",
            "Selector6": "begin end interval meters",
            "Selector7": "begin end interval meters",
            "Selector8": "begin end interval meters",
            "Selector9": "previous",
            "Selector10": "latest meters",
        },
    ),
    "connection": (
        "protocol::apdu",
        "protocol/apdu/connection.hpp",
        {
            "AssociationParameters": (
                "version protocol function send_frame_bytes receive_frame_bytes "
                "receive_window apdu_bytes timeout_seconds"
            ),
            "FactoryVersion": (
                "manufacturer software_version software_date hardware_version "
                "hardware_date extension"
            ),
            "NullSecurity": "",
            "PasswordSecurity": "password",
            "SymmetrySecurity": "ciphertext signature",
            "SignatureSecurity": "ciphertext signature",
            "SecurityData": "random signature",
            "LinkRequest": "piid_acd type heartbeat_seconds requested_at",
            "LinkResponse": "piid result requested_at received_at responded_at",
            "ConnectRequest": "piid parameters mechanism time_tag",
            "ConnectResponse": "piid_acd factory parameters result security time_tag follow_report",
            "ReleaseRequest": "piid time_tag",
            "ReleaseResponse": "piid_acd result time_tag follow_report",
            "ReleaseNotification": "piid_acd established_at current_time time_tag follow_report",
            "ErrorResponse": "server piid type time_tag follow_report",
        },
    ),
    "messages": (
        "protocol::apdu",
        "protocol/apdu/get.hpp",
        {
            "TimeTag": "sent_at allowed_delay",
            "GetRequest": "piid list attributes time_tag",
            "AttributeResult": "attribute result",
            "GetRecord": "attribute rows columns",
            "GetRecordRequest": "piid list records time_tag",
            "RecordResult": "attribute columns result",
            "GetResponse": "piid_acd list attributes time_tag follow_report",
            "GetRecordResponse": "piid_acd list records time_tag follow_report",
            "GetNextRequest": "piid block time_tag",
            "GetNextResponse": "piid_acd last block result time_tag follow_report",
            "GetMd5Request": "piid attribute time_tag",
            "GetMd5Response": "piid_acd attribute result time_tag follow_report",
        },
    ),
    "mutation": (
        "protocol::apdu",
        "protocol/apdu/mutation.hpp",
        {
            "SetAttribute": "attribute value",
            "SetResult": "attribute dar",
            "SetRequest": "piid list attributes time_tag",
            "SetResponse": "piid_acd list attributes time_tag follow_report",
            "ActionMethod": "method parameter",
            "ActionResult": "method dar data",
            "ActionRequest": "piid list methods time_tag",
            "ActionResponse": "piid_acd list methods time_tag follow_report",
            "SetThenGet": "set read delay_seconds",
            "ActionThenGet": "action read delay_seconds",
            "SetThenGetResult": "set read",
            "ActionThenGetResult": "action read",
            "SetThenGetRequest": "piid items time_tag",
            "SetThenGetResponse": "piid_acd items time_tag follow_report",
            "ActionThenGetRequest": "piid items time_tag",
            "ActionThenGetResponse": "piid_acd items time_tag follow_report",
        },
    ),
    "advanced": (
        "protocol::apdu",
        "protocol/apdu/advanced.hpp",
        {
            "TransData": "port data",
            "ReportNotification": "piid_acd payload time_tag follow_report",
            "ReportResponse": "piid choice attributes time_tag",
            "ProxyRecordRequest": "server record",
            "ProxyRecordResponse": "server record",
            "ProxyTransRequest": (
                "port communication response_timeout_seconds byte_timeout_milliseconds command"
            ),
            "ProxyTransResponse": "port result",
            "ProxyRequest": "piid timeout_seconds payload time_tag",
            "ProxyResponse": "piid_acd payload time_tag follow_report",
            "RnMac": "random mac",
            "SecurityRequest": "encrypted application verification",
            "SecurityResponse": "encrypted application verification",
        },
    ),
    "options": (
        "",
        "app/client.hpp",
        {
            "transport::ChannelOptions": (
                "read_chunk_bytes max_pending_write_bytes max_pending_writes"
            ),
            "transport::SerialOptions": "baud_rate data_bits parity stop_bits flow_control channel",
            "transport::SerialLinkOptions": (
                "baud_rate bits_per_character max_pending_write_bytes max_pending_writes"
            ),
            "protocol::link::ServerAddress": "type logical bytes",
            "protocol::link::Frame": "control server client payload",
            "session::SessionOptions": (
                "role server client_address limits parameters factory security_backend "
                "security_backend_factory "
                "protect_application request_timeout id_reuse_delay require_login "
                "preset_association "
                "clock_trusted heartbeat_seconds request_time_tag fragment_timeout "
                "reassembly_timeout fragment_retries report_retries prefer_get_blocks "
                "calendar_clock"
            ),
            "app::ClientOptions": "protocol transport_timeout login_timeout channel",
            "app::ServerOptions": "protocol max_connections heartbeat_seconds channel",
            "standard::DeviceLayout": "wiring tariff_count harmonic_order",
            "service::DeviceOptions": "layout limits max_objects max_attributes max_value_bytes",
            "service::AttributeSchema": "number type readable writable record",
            "service::MethodSchema": "number parameter_type return_type executable",
            "service::ObjectSchema": "oi name attributes methods",
            "service::ActionValue": "dar data",
        },
    ),
}


def generate() -> None:
    """生成显式字段绑定，编译会验证每个成员及字段类型。"""
    for group, (namespace, _header, classes) in GROUPS.items():
        text = '#include "bindings.hpp"\n'
        text += "#include <dlt698/app/client.hpp>\n#include <dlt698/app/server.hpp>\n"
        text += "#include <dlt698/standard/records.hpp>\n"
        text += "#include <dlt698/protocol/apdu/security.hpp>\n"
        if group == "options":
            text += '#include "options_callbacks.hpp"\n'
        text += "\nnamespace dlt698::python {\n"
        text += f"void bind_{group}(py::module_& module) {{\n"
        # 先注册本组全部类型，再生成属性签名，避免前向引用退化成 C++ 原始类型名。
        for name, _fields in classes.items():
            qualified = (namespace + "::" if namespace else "") + name
            short = name.split("::")[-1]
            text += f'    Struct<{qualified}> value_{short}(module, "{short}");\n'
        properties = ""
        for name, fields in classes.items():
            qualified = (namespace + "::" if namespace else "") + name
            short = name.split("::")[-1]
            for field in fields.split():
                properties += f'    value_{short}.field("{field}", &{qualified}::{field});\n'
            if short == "SerialLinkOptions":
                properties += "    serial_callbacks(value_SerialLinkOptions);\n"
            properties += f"    value_{short}.finish();\n"
        if group == "records":
            for field, typ in {
                "road": "Road",
                "region": "Region",
                "rsd": "Rsd",
                "csd": "Csd",
                "ms": "Ms",
                "rcsd": "Rcsd",
            }.items():
                text += (
                    '    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))'
                    f'.def_static("{field}", '
                    f"[](model::{typ} value) {{ return model::Data("
                    "model::RecordData(std::move(value))); });\n"
                )
        if group == "advanced":
            for short, typ in {
                "ProxyGetTarget": "model::Oad",
                "ProxyGetResultTarget": "protocol::apdu::AttributeResult",
                "ProxySetTarget": "protocol::apdu::SetAttribute",
                "ProxySetResultTarget": "protocol::apdu::SetResult",
                "ProxyActionTarget": "protocol::apdu::ActionMethod",
                "ProxyActionResultTarget": "protocol::apdu::ActionResult",
                "ProxySetThenGetTarget": "protocol::apdu::SetThenGet",
                "ProxySetThenGetResultTarget": "protocol::apdu::SetThenGetResult",
                "ProxyActionThenGetTarget": "protocol::apdu::ActionThenGet",
                "ProxyActionThenGetResultTarget": "protocol::apdu::ActionThenGetResult",
            }.items():
                qualified = f"protocol::apdu::ProxyTarget<{typ}>"
                text += (
                    f'    {{ using T = {qualified}; Struct<T> value(module, "{short}"); '
                    'value.field("server", &T::server)'
                    '.field("timeout_seconds", &T::timeout_seconds)'
                    '.field("items", &T::items).finish(); }\n'
                )
        text += properties + "}\n}  // namespace dlt698::python\n"
        layer = {"records": "model", "options": "app"}.get(group, "protocol/apdu")
        (ROOT / f"python/bindings/{layer}/{group}.cpp").write_text(text, encoding="utf-8")


if __name__ == "__main__":
    generate()
