#include "record_codec.h"

namespace ems::web {
using namespace database;

namespace {
Json encode(const CfgDeviceRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["device_id"] = row.device_id;
    j["parent_device_id"] = row.parent_device_id;
    j["node_kind"] = row.node_kind;
    j["code"] = row.code;
    j["name"] = row.name;
    j["path"] = row.path;
    j["depth"] = row.depth;
    j["sort_order"] = row.sort_order;
    j["device_type_code"] = row.device_type_code;
    j["type_version"] = row.type_version;
    j["template_id"] = row.template_id;
    j["source_device_id"] = row.source_device_id;
    j["vendor"] = row.vendor;
    j["model"] = row.model;
    j["firmware_version"] = row.firmware_version;
    j["rated_power_kw"] = row.rated_power_kw;
    j["timezone"] = row.timezone;
    j["default_stale_after_ms"] = row.default_stale_after_ms;
    j["config_revision"] = row.config_revision;
    j["enabled"] = row.enabled;
    j["description"] = row.description;
    j["legacy_ref"] = row.legacy_ref;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgDeviceRow decode_CfgDeviceRow(const Json& j) {
    CfgDeviceRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("device_id")) row.device_id = j.at("device_id").get<std::string>();
    if (j.count("parent_device_id"))
        row.parent_device_id = j.at("parent_device_id").get<std::string>();
    if (j.count("node_kind")) row.node_kind = j.at("node_kind").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("path")) row.path = j.at("path").get<std::string>();
    if (j.count("depth")) row.depth = j.at("depth").get<int>();
    if (j.count("sort_order")) row.sort_order = j.at("sort_order").get<int>();
    if (j.count("device_type_code"))
        row.device_type_code = j.at("device_type_code").get<std::string>();
    if (j.count("type_version")) row.type_version = j.at("type_version").get<std::string>();
    if (j.count("template_id")) row.template_id = j.at("template_id").get<std::string>();
    if (j.count("source_device_id"))
        row.source_device_id = j.at("source_device_id").get<std::string>();
    if (j.count("vendor")) row.vendor = j.at("vendor").get<std::string>();
    if (j.count("model")) row.model = j.at("model").get<std::string>();
    if (j.count("firmware_version"))
        row.firmware_version = j.at("firmware_version").get<std::string>();
    if (j.count("rated_power_kw")) row.rated_power_kw = j.at("rated_power_kw").get<double>();
    if (j.count("timezone")) row.timezone = j.at("timezone").get<std::string>();
    if (j.count("default_stale_after_ms"))
        row.default_stale_after_ms = j.at("default_stale_after_ms").get<int>();
    if (j.count("config_revision"))
        row.config_revision = j.at("config_revision").get<std::int64_t>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("legacy_ref")) row.legacy_ref = j.at("legacy_ref").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgDeviceTypeRow& row) {
    Json j = Json::object();
    j["type_code"] = row.type_code;
    j["type_version"] = row.type_version;
    j["name"] = row.name;
    j["category"] = row.category;
    j["status"] = row.status;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgDeviceTypeRow decode_CfgDeviceTypeRow(const Json& j) {
    CfgDeviceTypeRow row;
    if (j.count("type_code")) row.type_code = j.at("type_code").get<std::string>();
    if (j.count("type_version")) row.type_version = j.at("type_version").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("category")) row.category = j.at("category").get<std::string>();
    if (j.count("status")) row.status = j.at("status").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgTypePointRow& row) {
    Json j = Json::object();
    j["type_code"] = row.type_code;
    j["type_version"] = row.type_version;
    j["semantic_code"] = row.semantic_code;
    j["name"] = row.name;
    j["point_type"] = row.point_type;
    j["value_type"] = row.value_type;
    j["unit"] = row.unit;
    j["required"] = row.required;
    j["capability_codes_json"] = row.capability_codes_json;
    j["writable"] = row.writable;
    j["stale_after_ms"] = row.stale_after_ms;
    j["sort_order"] = row.sort_order;
    return j;
}

CfgTypePointRow decode_CfgTypePointRow(const Json& j) {
    CfgTypePointRow row;
    if (j.count("type_code")) row.type_code = j.at("type_code").get<std::string>();
    if (j.count("type_version")) row.type_version = j.at("type_version").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("point_type")) row.point_type = j.at("point_type").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("required")) row.required = j.at("required").get<int>();
    if (j.count("capability_codes_json"))
        row.capability_codes_json = j.at("capability_codes_json").get<std::string>();
    if (j.count("writable")) row.writable = j.at("writable").get<int>();
    if (j.count("stale_after_ms")) row.stale_after_ms = j.at("stale_after_ms").get<int>();
    if (j.count("sort_order")) row.sort_order = j.at("sort_order").get<int>();
    return row;
}

Json encode(const CfgTemplateRow& row) {
    Json j = Json::object();
    j["template_id"] = row.template_id;
    j["template_version"] = row.template_version;
    j["device_type_code"] = row.device_type_code;
    j["type_version"] = row.type_version;
    j["vendor"] = row.vendor;
    j["model"] = row.model;
    j["status"] = row.status;
    j["points_json"] = row.points_json;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgTemplateRow decode_CfgTemplateRow(const Json& j) {
    CfgTemplateRow row;
    if (j.count("template_id")) row.template_id = j.at("template_id").get<std::string>();
    if (j.count("template_version"))
        row.template_version = j.at("template_version").get<std::string>();
    if (j.count("device_type_code"))
        row.device_type_code = j.at("device_type_code").get<std::string>();
    if (j.count("type_version")) row.type_version = j.at("type_version").get<std::string>();
    if (j.count("vendor")) row.vendor = j.at("vendor").get<std::string>();
    if (j.count("model")) row.model = j.at("model").get<std::string>();
    if (j.count("status")) row.status = j.at("status").get<std::string>();
    if (j.count("points_json")) row.points_json = j.at("points_json").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgChannelRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["channel_id"] = row.channel_id;
    j["code"] = row.code;
    j["name"] = row.name;
    j["protocol"] = row.protocol;
    j["conn_mode"] = row.conn_mode;
    j["endpoint"] = row.endpoint;
    j["serial_json"] = row.serial_json;
    j["poll_interval_ms"] = row.poll_interval_ms;
    j["timeout_ms"] = row.timeout_ms;
    j["retry_times"] = row.retry_times;
    j["max_sessions"] = row.max_sessions;
    j["enabled"] = row.enabled;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgChannelRow decode_CfgChannelRow(const Json& j) {
    CfgChannelRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("channel_id")) row.channel_id = j.at("channel_id").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("protocol")) row.protocol = j.at("protocol").get<std::string>();
    if (j.count("conn_mode")) row.conn_mode = j.at("conn_mode").get<std::string>();
    if (j.count("endpoint")) row.endpoint = j.at("endpoint").get<std::string>();
    if (j.count("serial_json")) row.serial_json = j.at("serial_json").get<std::string>();
    if (j.count("poll_interval_ms")) row.poll_interval_ms = j.at("poll_interval_ms").get<int>();
    if (j.count("timeout_ms")) row.timeout_ms = j.at("timeout_ms").get<int>();
    if (j.count("retry_times")) row.retry_times = j.at("retry_times").get<int>();
    if (j.count("max_sessions")) row.max_sessions = j.at("max_sessions").get<int>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgBindingRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["binding_id"] = row.binding_id;
    j["device_id"] = row.device_id;
    j["channel_id"] = row.channel_id;
    j["device_address"] = row.device_address;
    j["profile_version"] = row.profile_version;
    j["can_collect"] = row.can_collect;
    j["can_control"] = row.can_control;
    j["can_forward"] = row.can_forward;
    j["enabled"] = row.enabled;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgBindingRow decode_CfgBindingRow(const Json& j) {
    CfgBindingRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("device_id")) row.device_id = j.at("device_id").get<std::string>();
    if (j.count("channel_id")) row.channel_id = j.at("channel_id").get<std::string>();
    if (j.count("device_address")) row.device_address = j.at("device_address").get<std::string>();
    if (j.count("profile_version"))
        row.profile_version = j.at("profile_version").get<std::string>();
    if (j.count("can_collect")) row.can_collect = j.at("can_collect").get<int>();
    if (j.count("can_control")) row.can_control = j.at("can_control").get<int>();
    if (j.count("can_forward")) row.can_forward = j.at("can_forward").get<int>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgPointYcRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["point_id"] = row.point_id;
    j["owner_device_id"] = row.owner_device_id;
    j["binding_id"] = row.binding_id;
    j["code"] = row.code;
    j["name"] = row.name;
    j["semantic_code"] = row.semantic_code;
    j["vendor_key"] = row.vendor_key;
    j["origin"] = row.origin;
    j["address_space"] = row.address_space;
    j["address_offset"] = row.address_offset;
    j["bit_offset"] = row.bit_offset;
    j["register_count"] = row.register_count;
    j["protocol_address_json"] = row.protocol_address_json;
    j["wire_type"] = row.wire_type;
    j["byte_order"] = row.byte_order;
    j["value_type"] = row.value_type;
    j["unit"] = row.unit;
    j["decimals"] = row.decimals;
    j["scale_factor"] = row.scale_factor;
    j["offset_value"] = row.offset_value;
    j["range_min"] = row.range_min;
    j["range_max"] = row.range_max;
    j["deadband"] = row.deadband;
    j["enabled"] = row.enabled;
    j["disabled_reason"] = row.disabled_reason;
    j["stale_after_ms"] = row.stale_after_ms;
    j["source_import_row_id"] = row.source_import_row_id;
    j["legacy_ref"] = row.legacy_ref;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgPointYcRow decode_CfgPointYcRow(const Json& j) {
    CfgPointYcRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("point_id")) row.point_id = j.at("point_id").get<std::string>();
    if (j.count("owner_device_id"))
        row.owner_device_id = j.at("owner_device_id").get<std::string>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("vendor_key")) row.vendor_key = j.at("vendor_key").get<std::string>();
    if (j.count("origin")) row.origin = j.at("origin").get<std::string>();
    if (j.count("address_space")) row.address_space = j.at("address_space").get<std::string>();
    if (j.count("address_offset")) row.address_offset = j.at("address_offset").get<int>();
    if (j.count("bit_offset")) row.bit_offset = j.at("bit_offset").get<int>();
    if (j.count("register_count")) row.register_count = j.at("register_count").get<int>();
    if (j.count("protocol_address_json"))
        row.protocol_address_json = j.at("protocol_address_json").get<std::string>();
    if (j.count("wire_type")) row.wire_type = j.at("wire_type").get<std::string>();
    if (j.count("byte_order")) row.byte_order = j.at("byte_order").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("decimals")) row.decimals = j.at("decimals").get<int>();
    if (j.count("scale_factor")) row.scale_factor = j.at("scale_factor").get<double>();
    if (j.count("offset_value")) row.offset_value = j.at("offset_value").get<double>();
    if (j.count("range_min")) row.range_min = j.at("range_min").get<double>();
    if (j.count("range_max")) row.range_max = j.at("range_max").get<double>();
    if (j.count("deadband")) row.deadband = j.at("deadband").get<double>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("disabled_reason"))
        row.disabled_reason = j.at("disabled_reason").get<std::string>();
    if (j.count("stale_after_ms")) row.stale_after_ms = j.at("stale_after_ms").get<int>();
    if (j.count("source_import_row_id"))
        row.source_import_row_id = j.at("source_import_row_id").get<std::string>();
    if (j.count("legacy_ref")) row.legacy_ref = j.at("legacy_ref").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgPointYxRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["point_id"] = row.point_id;
    j["owner_device_id"] = row.owner_device_id;
    j["binding_id"] = row.binding_id;
    j["code"] = row.code;
    j["name"] = row.name;
    j["semantic_code"] = row.semantic_code;
    j["vendor_key"] = row.vendor_key;
    j["origin"] = row.origin;
    j["address_space"] = row.address_space;
    j["address_offset"] = row.address_offset;
    j["bit_offset"] = row.bit_offset;
    j["register_count"] = row.register_count;
    j["protocol_address_json"] = row.protocol_address_json;
    j["wire_type"] = row.wire_type;
    j["byte_order"] = row.byte_order;
    j["value_type"] = row.value_type;
    j["unit"] = row.unit;
    j["soe"] = row.soe;
    j["invert"] = row.invert;
    j["enum_json"] = row.enum_json;
    j["unknown_policy"] = row.unknown_policy;
    j["feedback_of_point_id"] = row.feedback_of_point_id;
    j["enabled"] = row.enabled;
    j["disabled_reason"] = row.disabled_reason;
    j["stale_after_ms"] = row.stale_after_ms;
    j["source_import_row_id"] = row.source_import_row_id;
    j["legacy_ref"] = row.legacy_ref;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgPointYxRow decode_CfgPointYxRow(const Json& j) {
    CfgPointYxRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("point_id")) row.point_id = j.at("point_id").get<std::string>();
    if (j.count("owner_device_id"))
        row.owner_device_id = j.at("owner_device_id").get<std::string>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("vendor_key")) row.vendor_key = j.at("vendor_key").get<std::string>();
    if (j.count("origin")) row.origin = j.at("origin").get<std::string>();
    if (j.count("address_space")) row.address_space = j.at("address_space").get<std::string>();
    if (j.count("address_offset")) row.address_offset = j.at("address_offset").get<int>();
    if (j.count("bit_offset")) row.bit_offset = j.at("bit_offset").get<int>();
    if (j.count("register_count")) row.register_count = j.at("register_count").get<int>();
    if (j.count("protocol_address_json"))
        row.protocol_address_json = j.at("protocol_address_json").get<std::string>();
    if (j.count("wire_type")) row.wire_type = j.at("wire_type").get<std::string>();
    if (j.count("byte_order")) row.byte_order = j.at("byte_order").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("soe")) row.soe = j.at("soe").get<int>();
    if (j.count("invert")) row.invert = j.at("invert").get<int>();
    if (j.count("enum_json")) row.enum_json = j.at("enum_json").get<std::string>();
    if (j.count("unknown_policy")) row.unknown_policy = j.at("unknown_policy").get<std::string>();
    if (j.count("feedback_of_point_id"))
        row.feedback_of_point_id = j.at("feedback_of_point_id").get<std::string>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("disabled_reason"))
        row.disabled_reason = j.at("disabled_reason").get<std::string>();
    if (j.count("stale_after_ms")) row.stale_after_ms = j.at("stale_after_ms").get<int>();
    if (j.count("source_import_row_id"))
        row.source_import_row_id = j.at("source_import_row_id").get<std::string>();
    if (j.count("legacy_ref")) row.legacy_ref = j.at("legacy_ref").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgPointYkRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["point_id"] = row.point_id;
    j["owner_device_id"] = row.owner_device_id;
    j["binding_id"] = row.binding_id;
    j["code"] = row.code;
    j["name"] = row.name;
    j["semantic_code"] = row.semantic_code;
    j["vendor_key"] = row.vendor_key;
    j["origin"] = row.origin;
    j["address_space"] = row.address_space;
    j["address_offset"] = row.address_offset;
    j["bit_offset"] = row.bit_offset;
    j["register_count"] = row.register_count;
    j["protocol_address_json"] = row.protocol_address_json;
    j["wire_type"] = row.wire_type;
    j["byte_order"] = row.byte_order;
    j["value_type"] = row.value_type;
    j["unit"] = row.unit;
    j["write_mode"] = row.write_mode;
    j["fixed_raw_value"] = row.fixed_raw_value;
    j["pulse_width_ms"] = row.pulse_width_ms;
    j["min_interval_ms"] = row.min_interval_ms;
    j["write_group_key"] = row.write_group_key;
    j["feedback_json"] = row.feedback_json;
    j["feedback_timeout_ms"] = row.feedback_timeout_ms;
    j["failure_policy"] = row.failure_policy;
    j["enabled"] = row.enabled;
    j["disabled_reason"] = row.disabled_reason;
    j["stale_after_ms"] = row.stale_after_ms;
    j["source_import_row_id"] = row.source_import_row_id;
    j["legacy_ref"] = row.legacy_ref;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgPointYkRow decode_CfgPointYkRow(const Json& j) {
    CfgPointYkRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("point_id")) row.point_id = j.at("point_id").get<std::string>();
    if (j.count("owner_device_id"))
        row.owner_device_id = j.at("owner_device_id").get<std::string>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("vendor_key")) row.vendor_key = j.at("vendor_key").get<std::string>();
    if (j.count("origin")) row.origin = j.at("origin").get<std::string>();
    if (j.count("address_space")) row.address_space = j.at("address_space").get<std::string>();
    if (j.count("address_offset")) row.address_offset = j.at("address_offset").get<int>();
    if (j.count("bit_offset")) row.bit_offset = j.at("bit_offset").get<int>();
    if (j.count("register_count")) row.register_count = j.at("register_count").get<int>();
    if (j.count("protocol_address_json"))
        row.protocol_address_json = j.at("protocol_address_json").get<std::string>();
    if (j.count("wire_type")) row.wire_type = j.at("wire_type").get<std::string>();
    if (j.count("byte_order")) row.byte_order = j.at("byte_order").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("write_mode")) row.write_mode = j.at("write_mode").get<std::string>();
    if (j.count("fixed_raw_value"))
        row.fixed_raw_value = j.at("fixed_raw_value").get<std::int64_t>();
    if (j.count("pulse_width_ms")) row.pulse_width_ms = j.at("pulse_width_ms").get<int>();
    if (j.count("min_interval_ms")) row.min_interval_ms = j.at("min_interval_ms").get<int>();
    if (j.count("write_group_key"))
        row.write_group_key = j.at("write_group_key").get<std::string>();
    if (j.count("feedback_json")) row.feedback_json = j.at("feedback_json").get<std::string>();
    if (j.count("feedback_timeout_ms"))
        row.feedback_timeout_ms = j.at("feedback_timeout_ms").get<int>();
    if (j.count("failure_policy")) row.failure_policy = j.at("failure_policy").get<std::string>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("disabled_reason"))
        row.disabled_reason = j.at("disabled_reason").get<std::string>();
    if (j.count("stale_after_ms")) row.stale_after_ms = j.at("stale_after_ms").get<int>();
    if (j.count("source_import_row_id"))
        row.source_import_row_id = j.at("source_import_row_id").get<std::string>();
    if (j.count("legacy_ref")) row.legacy_ref = j.at("legacy_ref").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgPointYtRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["point_id"] = row.point_id;
    j["owner_device_id"] = row.owner_device_id;
    j["binding_id"] = row.binding_id;
    j["code"] = row.code;
    j["name"] = row.name;
    j["semantic_code"] = row.semantic_code;
    j["vendor_key"] = row.vendor_key;
    j["origin"] = row.origin;
    j["address_space"] = row.address_space;
    j["address_offset"] = row.address_offset;
    j["bit_offset"] = row.bit_offset;
    j["register_count"] = row.register_count;
    j["protocol_address_json"] = row.protocol_address_json;
    j["wire_type"] = row.wire_type;
    j["byte_order"] = row.byte_order;
    j["value_type"] = row.value_type;
    j["unit"] = row.unit;
    j["decimals"] = row.decimals;
    j["scale_factor"] = row.scale_factor;
    j["offset_value"] = row.offset_value;
    j["range_min"] = row.range_min;
    j["range_max"] = row.range_max;
    j["min_interval_ms"] = row.min_interval_ms;
    j["write_group_key"] = row.write_group_key;
    j["feedback_json"] = row.feedback_json;
    j["feedback_timeout_ms"] = row.feedback_timeout_ms;
    j["failure_policy"] = row.failure_policy;
    j["enabled"] = row.enabled;
    j["disabled_reason"] = row.disabled_reason;
    j["stale_after_ms"] = row.stale_after_ms;
    j["source_import_row_id"] = row.source_import_row_id;
    j["legacy_ref"] = row.legacy_ref;
    j["description"] = row.description;
    j["created_at"] = row.created_at;
    j["updated_at"] = row.updated_at;
    return j;
}

CfgPointYtRow decode_CfgPointYtRow(const Json& j) {
    CfgPointYtRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("point_id")) row.point_id = j.at("point_id").get<std::string>();
    if (j.count("owner_device_id"))
        row.owner_device_id = j.at("owner_device_id").get<std::string>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("code")) row.code = j.at("code").get<std::string>();
    if (j.count("name")) row.name = j.at("name").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("vendor_key")) row.vendor_key = j.at("vendor_key").get<std::string>();
    if (j.count("origin")) row.origin = j.at("origin").get<std::string>();
    if (j.count("address_space")) row.address_space = j.at("address_space").get<std::string>();
    if (j.count("address_offset")) row.address_offset = j.at("address_offset").get<int>();
    if (j.count("bit_offset")) row.bit_offset = j.at("bit_offset").get<int>();
    if (j.count("register_count")) row.register_count = j.at("register_count").get<int>();
    if (j.count("protocol_address_json"))
        row.protocol_address_json = j.at("protocol_address_json").get<std::string>();
    if (j.count("wire_type")) row.wire_type = j.at("wire_type").get<std::string>();
    if (j.count("byte_order")) row.byte_order = j.at("byte_order").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("decimals")) row.decimals = j.at("decimals").get<int>();
    if (j.count("scale_factor")) row.scale_factor = j.at("scale_factor").get<double>();
    if (j.count("offset_value")) row.offset_value = j.at("offset_value").get<double>();
    if (j.count("range_min")) row.range_min = j.at("range_min").get<double>();
    if (j.count("range_max")) row.range_max = j.at("range_max").get<double>();
    if (j.count("min_interval_ms")) row.min_interval_ms = j.at("min_interval_ms").get<int>();
    if (j.count("write_group_key"))
        row.write_group_key = j.at("write_group_key").get<std::string>();
    if (j.count("feedback_json")) row.feedback_json = j.at("feedback_json").get<std::string>();
    if (j.count("feedback_timeout_ms"))
        row.feedback_timeout_ms = j.at("feedback_timeout_ms").get<int>();
    if (j.count("failure_policy")) row.failure_policy = j.at("failure_policy").get<std::string>();
    if (j.count("enabled")) row.enabled = j.at("enabled").get<int>();
    if (j.count("disabled_reason"))
        row.disabled_reason = j.at("disabled_reason").get<std::string>();
    if (j.count("stale_after_ms")) row.stale_after_ms = j.at("stale_after_ms").get<int>();
    if (j.count("source_import_row_id"))
        row.source_import_row_id = j.at("source_import_row_id").get<std::string>();
    if (j.count("legacy_ref")) row.legacy_ref = j.at("legacy_ref").get<std::string>();
    if (j.count("description")) row.description = j.at("description").get<std::string>();
    if (j.count("created_at")) row.created_at = j.at("created_at").get<std::string>();
    if (j.count("updated_at")) row.updated_at = j.at("updated_at").get<std::string>();
    return row;
}

Json encode(const CfgImportJobRow& row) {
    Json j = Json::object();
    j["import_job_id"] = row.import_job_id;
    j["device_id"] = row.device_id;
    j["binding_id"] = row.binding_id;
    j["source_kind"] = row.source_kind;
    j["file_name"] = row.file_name;
    j["file_sha256"] = row.file_sha256;
    j["file_bytes"] = row.file_bytes;
    j["address_base"] = row.address_base;
    j["status"] = row.status;
    j["row_total"] = row.row_total;
    j["row_ok"] = row.row_ok;
    j["row_warning"] = row.row_warning;
    j["row_error"] = row.row_error;
    j["idempotency_key"] = row.idempotency_key;
    j["submitted_by"] = row.submitted_by;
    j["submitted_at"] = row.submitted_at;
    j["finished_at"] = row.finished_at;
    j["error_code"] = row.error_code;
    j["error_message"] = row.error_message;
    return j;
}

CfgImportJobRow decode_CfgImportJobRow(const Json& j) {
    CfgImportJobRow row;
    if (j.count("import_job_id")) row.import_job_id = j.at("import_job_id").get<std::string>();
    if (j.count("device_id")) row.device_id = j.at("device_id").get<std::string>();
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("source_kind")) row.source_kind = j.at("source_kind").get<std::string>();
    if (j.count("file_name")) row.file_name = j.at("file_name").get<std::string>();
    if (j.count("file_sha256")) row.file_sha256 = j.at("file_sha256").get<std::string>();
    if (j.count("file_bytes")) row.file_bytes = j.at("file_bytes").get<int>();
    if (j.count("address_base")) row.address_base = j.at("address_base").get<std::string>();
    if (j.count("status")) row.status = j.at("status").get<std::string>();
    if (j.count("row_total")) row.row_total = j.at("row_total").get<int>();
    if (j.count("row_ok")) row.row_ok = j.at("row_ok").get<int>();
    if (j.count("row_warning")) row.row_warning = j.at("row_warning").get<int>();
    if (j.count("row_error")) row.row_error = j.at("row_error").get<int>();
    if (j.count("idempotency_key"))
        row.idempotency_key = j.at("idempotency_key").get<std::string>();
    if (j.count("submitted_by")) row.submitted_by = j.at("submitted_by").get<std::string>();
    if (j.count("submitted_at")) row.submitted_at = j.at("submitted_at").get<std::string>();
    if (j.count("finished_at")) row.finished_at = j.at("finished_at").get<std::string>();
    if (j.count("error_code")) row.error_code = j.at("error_code").get<std::string>();
    if (j.count("error_message")) row.error_message = j.at("error_message").get<std::string>();
    return row;
}

Json encode(const CfgImportRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["import_job_id"] = row.import_job_id;
    j["line_no"] = row.line_no;
    j["vendor_key"] = row.vendor_key;
    j["vendor_name"] = row.vendor_name;
    j["point_type"] = row.point_type;
    j["target_device_path"] = row.target_device_path;
    j["semantic_code"] = row.semantic_code;
    j["address_space"] = row.address_space;
    j["address_offset"] = row.address_offset;
    j["bit_offset"] = row.bit_offset;
    j["register_count"] = row.register_count;
    j["protocol_address_json"] = row.protocol_address_json;
    j["wire_type"] = row.wire_type;
    j["byte_order"] = row.byte_order;
    j["value_type"] = row.value_type;
    j["scale_factor"] = row.scale_factor;
    j["offset_value"] = row.offset_value;
    j["unit"] = row.unit;
    j["parse_status"] = row.parse_status;
    j["parse_code"] = row.parse_code;
    j["parse_message"] = row.parse_message;
    j["confirm_state"] = row.confirm_state;
    j["confirm_note"] = row.confirm_note;
    j["confirm_by"] = row.confirm_by;
    j["confirm_at"] = row.confirm_at;
    j["resolved_point_id"] = row.resolved_point_id;
    return j;
}

CfgImportRow decode_CfgImportRow(const Json& j) {
    CfgImportRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("import_job_id")) row.import_job_id = j.at("import_job_id").get<std::string>();
    if (j.count("line_no")) row.line_no = j.at("line_no").get<int>();
    if (j.count("vendor_key")) row.vendor_key = j.at("vendor_key").get<std::string>();
    if (j.count("vendor_name")) row.vendor_name = j.at("vendor_name").get<std::string>();
    if (j.count("point_type")) row.point_type = j.at("point_type").get<std::string>();
    if (j.count("target_device_path"))
        row.target_device_path = j.at("target_device_path").get<std::string>();
    if (j.count("semantic_code")) row.semantic_code = j.at("semantic_code").get<std::string>();
    if (j.count("address_space")) row.address_space = j.at("address_space").get<std::string>();
    if (j.count("address_offset")) row.address_offset = j.at("address_offset").get<int>();
    if (j.count("bit_offset")) row.bit_offset = j.at("bit_offset").get<int>();
    if (j.count("register_count")) row.register_count = j.at("register_count").get<int>();
    if (j.count("protocol_address_json"))
        row.protocol_address_json = j.at("protocol_address_json").get<std::string>();
    if (j.count("wire_type")) row.wire_type = j.at("wire_type").get<std::string>();
    if (j.count("byte_order")) row.byte_order = j.at("byte_order").get<std::string>();
    if (j.count("value_type")) row.value_type = j.at("value_type").get<std::string>();
    if (j.count("scale_factor")) row.scale_factor = j.at("scale_factor").get<double>();
    if (j.count("offset_value")) row.offset_value = j.at("offset_value").get<double>();
    if (j.count("unit")) row.unit = j.at("unit").get<std::string>();
    if (j.count("parse_status")) row.parse_status = j.at("parse_status").get<std::string>();
    if (j.count("parse_code")) row.parse_code = j.at("parse_code").get<std::string>();
    if (j.count("parse_message")) row.parse_message = j.at("parse_message").get<std::string>();
    if (j.count("confirm_state")) row.confirm_state = j.at("confirm_state").get<std::string>();
    if (j.count("confirm_note")) row.confirm_note = j.at("confirm_note").get<std::string>();
    if (j.count("confirm_by")) row.confirm_by = j.at("confirm_by").get<std::string>();
    if (j.count("confirm_at")) row.confirm_at = j.at("confirm_at").get<std::string>();
    if (j.count("resolved_point_id"))
        row.resolved_point_id = j.at("resolved_point_id").get<std::string>();
    return row;
}

Json encode(const OpsCommStat5mRow& row) {
    Json j = Json::object();
    j["binding_id"] = row.binding_id;
    j["window_start"] = row.window_start;
    j["request_total"] = row.request_total;
    j["success_total"] = row.success_total;
    j["timeout_total"] = row.timeout_total;
    j["error_total"] = row.error_total;
    j["avg_latency_ms"] = row.avg_latency_ms;
    j["p95_latency_ms"] = row.p95_latency_ms;
    j["last_valid_at"] = row.last_valid_at;
    j["last_error_code"] = row.last_error_code;
    return j;
}

OpsCommStat5mRow decode_OpsCommStat5mRow(const Json& j) {
    OpsCommStat5mRow row;
    if (j.count("binding_id")) row.binding_id = j.at("binding_id").get<std::string>();
    if (j.count("window_start")) row.window_start = j.at("window_start").get<std::string>();
    if (j.count("request_total")) row.request_total = j.at("request_total").get<std::int64_t>();
    if (j.count("success_total")) row.success_total = j.at("success_total").get<std::int64_t>();
    if (j.count("timeout_total")) row.timeout_total = j.at("timeout_total").get<std::int64_t>();
    if (j.count("error_total")) row.error_total = j.at("error_total").get<std::int64_t>();
    if (j.count("avg_latency_ms")) row.avg_latency_ms = j.at("avg_latency_ms").get<int>();
    if (j.count("p95_latency_ms")) row.p95_latency_ms = j.at("p95_latency_ms").get<int>();
    if (j.count("last_valid_at")) row.last_valid_at = j.at("last_valid_at").get<std::string>();
    if (j.count("last_error_code"))
        row.last_error_code = j.at("last_error_code").get<std::string>();
    return row;
}

Json encode(const OpsStateLogRow& row) {
    Json j = Json::object();
    j["id"] = row.id;
    j["object_kind"] = row.object_kind;
    j["object_id"] = row.object_id;
    j["state"] = row.state;
    j["since"] = row.since;
    j["reason"] = row.reason;
    return j;
}

OpsStateLogRow decode_OpsStateLogRow(const Json& j) {
    OpsStateLogRow row;
    if (j.count("id")) row.id = j.at("id").get<std::int64_t>();
    if (j.count("object_kind")) row.object_kind = j.at("object_kind").get<std::string>();
    if (j.count("object_id")) row.object_id = j.at("object_id").get<std::string>();
    if (j.count("state")) row.state = j.at("state").get<std::string>();
    if (j.count("since")) row.since = j.at("since").get<std::string>();
    if (j.count("reason")) row.reason = j.at("reason").get<std::string>();
    return row;
}
}  // namespace

Json encode_record(const DeviceManagementRecord& row) {
    return std::visit([](const auto& value) { return encode(value); }, row);
}

DeviceManagementRecord decode_record(DeviceTable table, const Json& json) {
    switch (table) {
        case DeviceTable::Device:
            return decode_CfgDeviceRow(json);
        case DeviceTable::Type:
            return decode_CfgDeviceTypeRow(json);
        case DeviceTable::TypePoint:
            return decode_CfgTypePointRow(json);
        case DeviceTable::Template:
            return decode_CfgTemplateRow(json);
        case DeviceTable::Channel:
            return decode_CfgChannelRow(json);
        case DeviceTable::Binding:
            return decode_CfgBindingRow(json);
        case DeviceTable::Yc:
            return decode_CfgPointYcRow(json);
        case DeviceTable::Yx:
            return decode_CfgPointYxRow(json);
        case DeviceTable::Yk:
            return decode_CfgPointYkRow(json);
        case DeviceTable::Yt:
            return decode_CfgPointYtRow(json);
        case DeviceTable::ImportJob:
            return decode_CfgImportJobRow(json);
        case DeviceTable::ImportRow:
            return decode_CfgImportRow(json);
        case DeviceTable::CommStat:
            return decode_OpsCommStat5mRow(json);
        case DeviceTable::StateLog:
            return decode_OpsStateLogRow(json);
    }
    throw std::invalid_argument("unknown device table");
}
}  // namespace ems::web
