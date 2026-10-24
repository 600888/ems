#pragma once
#include <optional>

#include "device_management_rows.h"

namespace ems::database::detail {
// 写入专用映射将公开行的空字符串转换为 SQL NULL，避免空串写进 ENUM/JSON/DATETIME。
struct CfgDeviceRowWrite {
    std::int64_t id;
    std::string device_id;
    std::string parent_device_id;
    std::string node_kind;
    std::string code;
    std::string name;
    std::string path;
    int depth;
    int sort_order;
    std::string device_type_code;
    std::string type_version;
    std::string template_id;
    std::string source_device_id;
    std::string vendor;
    std::string model;
    std::string firmware_version;
    std::optional<double> rated_power_kw;
    std::string timezone;
    int default_stale_after_ms;
    std::int64_t config_revision;
    int enabled;
    std::string description;
    std::optional<std::string> legacy_ref;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgDeviceRowWrite*) { return "cfg_device"; }
YLT_REFL(CfgDeviceRowWrite, id, device_id, parent_device_id, node_kind, code, name, path, depth,
         sort_order, device_type_code, type_version, template_id, source_device_id, vendor, model,
         firmware_version, rated_power_kw, timezone, default_stale_after_ms, config_revision,
         enabled, description, legacy_ref, created_at, updated_at)
inline const auto CfgDeviceRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgDeviceRowWrite>(), "id");
inline const auto CfgDeviceRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgDeviceRowWrite>(), "device_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgDeviceRowWrite sql_row(const CfgDeviceRow& row) {
    CfgDeviceRowWrite value;
    value.id = row.id;
    value.device_id = row.device_id;
    value.parent_device_id = row.parent_device_id;
    value.node_kind = row.node_kind;
    value.code = row.code;
    value.name = row.name;
    value.path = row.path;
    value.depth = row.depth;
    value.sort_order = row.sort_order;
    value.device_type_code = row.device_type_code;
    value.type_version = row.type_version;
    value.template_id = row.template_id;
    value.source_device_id = row.source_device_id;
    value.vendor = row.vendor;
    value.model = row.model;
    value.firmware_version = row.firmware_version;
    if (!(false)) value.rated_power_kw = row.rated_power_kw;
    value.timezone = row.timezone;
    value.default_stale_after_ms = row.default_stale_after_ms;
    value.config_revision = row.config_revision;
    value.enabled = row.enabled;
    value.description = row.description;
    if (!(row.legacy_ref.empty())) value.legacy_ref = row.legacy_ref;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgChannelRowWrite {
    std::int64_t id;
    std::string channel_id;
    std::string code;
    std::string name;
    std::string protocol;
    std::string conn_mode;
    std::string endpoint;
    std::optional<std::string> serial_json;
    int poll_interval_ms;
    int timeout_ms;
    int retry_times;
    int max_sessions;
    int enabled;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgChannelRowWrite*) { return "cfg_channel"; }
YLT_REFL(CfgChannelRowWrite, id, channel_id, code, name, protocol, conn_mode, endpoint, serial_json,
         poll_interval_ms, timeout_ms, retry_times, max_sessions, enabled, description, created_at,
         updated_at)
inline const auto CfgChannelRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgChannelRowWrite>(), "id");
inline const auto CfgChannelRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgChannelRowWrite>(), "channel_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgChannelRowWrite sql_row(const CfgChannelRow& row) {
    CfgChannelRowWrite value;
    value.id = row.id;
    value.channel_id = row.channel_id;
    value.code = row.code;
    value.name = row.name;
    value.protocol = row.protocol;
    value.conn_mode = row.conn_mode;
    value.endpoint = row.endpoint;
    if (!(row.serial_json.empty())) value.serial_json = row.serial_json;
    value.poll_interval_ms = row.poll_interval_ms;
    value.timeout_ms = row.timeout_ms;
    value.retry_times = row.retry_times;
    value.max_sessions = row.max_sessions;
    value.enabled = row.enabled;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgBindingRowWrite {
    std::int64_t id;
    std::string binding_id;
    std::string device_id;
    std::string channel_id;
    std::string device_address;
    std::string profile_version;
    int can_collect;
    int can_control;
    int can_forward;
    int enabled;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgBindingRowWrite*) { return "cfg_binding"; }
YLT_REFL(CfgBindingRowWrite, id, binding_id, device_id, channel_id, device_address, profile_version,
         can_collect, can_control, can_forward, enabled, description, created_at, updated_at)
inline const auto CfgBindingRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgBindingRowWrite>(), "id");
inline const auto CfgBindingRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgBindingRowWrite>(), "binding_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgBindingRowWrite sql_row(const CfgBindingRow& row) {
    CfgBindingRowWrite value;
    value.id = row.id;
    value.binding_id = row.binding_id;
    value.device_id = row.device_id;
    value.channel_id = row.channel_id;
    value.device_address = row.device_address;
    value.profile_version = row.profile_version;
    value.can_collect = row.can_collect;
    value.can_control = row.can_control;
    value.can_forward = row.can_forward;
    value.enabled = row.enabled;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgPointYcRowWrite {
    std::int64_t id;
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::string address_space;
    int address_offset;
    int bit_offset;
    int register_count;
    std::optional<std::string> protocol_address_json;
    std::string wire_type;
    std::optional<std::string> byte_order;
    std::string value_type;
    std::string unit;
    int decimals;
    double scale_factor;
    double offset_value;
    double range_min;
    double range_max;
    double deadband;
    int enabled;
    std::string disabled_reason;
    int stale_after_ms;
    std::string source_import_row_id;
    std::optional<std::string> legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgPointYcRowWrite*) { return "cfg_point_yc"; }
YLT_REFL(CfgPointYcRowWrite, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, decimals, scale_factor,
         offset_value, range_min, range_max, deadband, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)
inline const auto CfgPointYcRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYcRowWrite>(), "id");
inline const auto CfgPointYcRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYcRowWrite>(), "point_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgPointYcRowWrite sql_row(const CfgPointYcRow& row) {
    CfgPointYcRowWrite value;
    value.id = row.id;
    value.point_id = row.point_id;
    value.owner_device_id = row.owner_device_id;
    value.binding_id = row.binding_id;
    value.code = row.code;
    value.name = row.name;
    value.semantic_code = row.semantic_code;
    value.vendor_key = row.vendor_key;
    value.origin = row.origin;
    value.address_space = row.address_space;
    value.address_offset = row.address_offset;
    value.bit_offset = row.bit_offset;
    value.register_count = row.register_count;
    if (!(row.protocol_address_json.empty()))
        value.protocol_address_json = row.protocol_address_json;
    value.wire_type = row.wire_type;
    if (!(row.byte_order.empty())) value.byte_order = row.byte_order;
    value.value_type = row.value_type;
    value.unit = row.unit;
    value.decimals = row.decimals;
    value.scale_factor = row.scale_factor;
    value.offset_value = row.offset_value;
    value.range_min = row.range_min;
    value.range_max = row.range_max;
    value.deadband = row.deadband;
    value.enabled = row.enabled;
    value.disabled_reason = row.disabled_reason;
    value.stale_after_ms = row.stale_after_ms;
    value.source_import_row_id = row.source_import_row_id;
    if (!(row.legacy_ref.empty())) value.legacy_ref = row.legacy_ref;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgPointYxRowWrite {
    std::int64_t id;
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::optional<std::string> address_space;
    int address_offset;
    int bit_offset;
    int register_count;
    std::optional<std::string> protocol_address_json;
    std::string wire_type;
    std::optional<std::string> byte_order;
    std::string value_type;
    std::string unit;
    int soe;
    int invert;
    std::optional<std::string> enum_json;
    std::string unknown_policy;
    std::string feedback_of_point_id;
    int enabled;
    std::string disabled_reason;
    int stale_after_ms;
    std::string source_import_row_id;
    std::optional<std::string> legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgPointYxRowWrite*) { return "cfg_point_yx"; }
YLT_REFL(CfgPointYxRowWrite, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, soe, invert, enum_json,
         unknown_policy, feedback_of_point_id, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)
inline const auto CfgPointYxRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYxRowWrite>(), "id");
inline const auto CfgPointYxRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYxRowWrite>(), "point_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgPointYxRowWrite sql_row(const CfgPointYxRow& row) {
    CfgPointYxRowWrite value;
    value.id = row.id;
    value.point_id = row.point_id;
    value.owner_device_id = row.owner_device_id;
    value.binding_id = row.binding_id;
    value.code = row.code;
    value.name = row.name;
    value.semantic_code = row.semantic_code;
    value.vendor_key = row.vendor_key;
    value.origin = row.origin;
    if (!(row.address_space.empty())) value.address_space = row.address_space;
    value.address_offset = row.address_offset;
    value.bit_offset = row.bit_offset;
    value.register_count = row.register_count;
    if (!(row.protocol_address_json.empty()))
        value.protocol_address_json = row.protocol_address_json;
    value.wire_type = row.wire_type;
    if (!(row.byte_order.empty())) value.byte_order = row.byte_order;
    value.value_type = row.value_type;
    value.unit = row.unit;
    value.soe = row.soe;
    value.invert = row.invert;
    if (!(row.enum_json.empty())) value.enum_json = row.enum_json;
    value.unknown_policy = row.unknown_policy;
    value.feedback_of_point_id = row.feedback_of_point_id;
    value.enabled = row.enabled;
    value.disabled_reason = row.disabled_reason;
    value.stale_after_ms = row.stale_after_ms;
    value.source_import_row_id = row.source_import_row_id;
    if (!(row.legacy_ref.empty())) value.legacy_ref = row.legacy_ref;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgPointYkRowWrite {
    std::int64_t id;
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::optional<std::string> address_space;
    int address_offset;
    int bit_offset;
    int register_count;
    std::optional<std::string> protocol_address_json;
    std::string wire_type;
    std::optional<std::string> byte_order;
    std::string value_type;
    std::string unit;
    std::string write_mode;
    std::int64_t fixed_raw_value;
    int pulse_width_ms;
    int min_interval_ms;
    std::string write_group_key;
    std::optional<std::string> feedback_json;
    int feedback_timeout_ms;
    std::string failure_policy;
    int enabled;
    std::string disabled_reason;
    int stale_after_ms;
    std::string source_import_row_id;
    std::optional<std::string> legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgPointYkRowWrite*) { return "cfg_point_yk"; }
YLT_REFL(CfgPointYkRowWrite, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, write_mode,
         fixed_raw_value, pulse_width_ms, min_interval_ms, write_group_key, feedback_json,
         feedback_timeout_ms, failure_policy, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)
inline const auto CfgPointYkRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYkRowWrite>(), "id");
inline const auto CfgPointYkRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYkRowWrite>(), "point_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgPointYkRowWrite sql_row(const CfgPointYkRow& row) {
    CfgPointYkRowWrite value;
    value.id = row.id;
    value.point_id = row.point_id;
    value.owner_device_id = row.owner_device_id;
    value.binding_id = row.binding_id;
    value.code = row.code;
    value.name = row.name;
    value.semantic_code = row.semantic_code;
    value.vendor_key = row.vendor_key;
    value.origin = row.origin;
    if (!(row.address_space.empty())) value.address_space = row.address_space;
    value.address_offset = row.address_offset;
    value.bit_offset = row.bit_offset;
    value.register_count = row.register_count;
    if (!(row.protocol_address_json.empty()))
        value.protocol_address_json = row.protocol_address_json;
    value.wire_type = row.wire_type;
    if (!(row.byte_order.empty())) value.byte_order = row.byte_order;
    value.value_type = row.value_type;
    value.unit = row.unit;
    value.write_mode = row.write_mode;
    value.fixed_raw_value = row.fixed_raw_value;
    value.pulse_width_ms = row.pulse_width_ms;
    value.min_interval_ms = row.min_interval_ms;
    value.write_group_key = row.write_group_key;
    if (!(row.feedback_json.empty())) value.feedback_json = row.feedback_json;
    value.feedback_timeout_ms = row.feedback_timeout_ms;
    value.failure_policy = row.failure_policy;
    value.enabled = row.enabled;
    value.disabled_reason = row.disabled_reason;
    value.stale_after_ms = row.stale_after_ms;
    value.source_import_row_id = row.source_import_row_id;
    if (!(row.legacy_ref.empty())) value.legacy_ref = row.legacy_ref;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgPointYtRowWrite {
    std::int64_t id;
    std::string point_id;
    std::string owner_device_id;
    std::string binding_id;
    std::string code;
    std::string name;
    std::string semantic_code;
    std::string vendor_key;
    std::string origin;
    std::optional<std::string> address_space;
    int address_offset;
    int bit_offset;
    int register_count;
    std::optional<std::string> protocol_address_json;
    std::string wire_type;
    std::optional<std::string> byte_order;
    std::string value_type;
    std::string unit;
    int decimals;
    double scale_factor;
    double offset_value;
    double range_min;
    double range_max;
    int min_interval_ms;
    std::string write_group_key;
    std::optional<std::string> feedback_json;
    int feedback_timeout_ms;
    std::string failure_policy;
    int enabled;
    std::string disabled_reason;
    int stale_after_ms;
    std::string source_import_row_id;
    std::optional<std::string> legacy_ref;
    std::string description;
    std::string created_at;
    std::string updated_at;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgPointYtRowWrite*) { return "cfg_point_yt"; }
YLT_REFL(CfgPointYtRowWrite, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, decimals, scale_factor,
         offset_value, range_min, range_max, min_interval_ms, write_group_key, feedback_json,
         feedback_timeout_ms, failure_policy, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)
inline const auto CfgPointYtRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYtRowWrite>(), "id");
inline const auto CfgPointYtRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYtRowWrite>(), "point_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgPointYtRowWrite sql_row(const CfgPointYtRow& row) {
    CfgPointYtRowWrite value;
    value.id = row.id;
    value.point_id = row.point_id;
    value.owner_device_id = row.owner_device_id;
    value.binding_id = row.binding_id;
    value.code = row.code;
    value.name = row.name;
    value.semantic_code = row.semantic_code;
    value.vendor_key = row.vendor_key;
    value.origin = row.origin;
    if (!(row.address_space.empty())) value.address_space = row.address_space;
    value.address_offset = row.address_offset;
    value.bit_offset = row.bit_offset;
    value.register_count = row.register_count;
    if (!(row.protocol_address_json.empty()))
        value.protocol_address_json = row.protocol_address_json;
    value.wire_type = row.wire_type;
    if (!(row.byte_order.empty())) value.byte_order = row.byte_order;
    value.value_type = row.value_type;
    value.unit = row.unit;
    value.decimals = row.decimals;
    value.scale_factor = row.scale_factor;
    value.offset_value = row.offset_value;
    value.range_min = row.range_min;
    value.range_max = row.range_max;
    value.min_interval_ms = row.min_interval_ms;
    value.write_group_key = row.write_group_key;
    if (!(row.feedback_json.empty())) value.feedback_json = row.feedback_json;
    value.feedback_timeout_ms = row.feedback_timeout_ms;
    value.failure_policy = row.failure_policy;
    value.enabled = row.enabled;
    value.disabled_reason = row.disabled_reason;
    value.stale_after_ms = row.stale_after_ms;
    value.source_import_row_id = row.source_import_row_id;
    if (!(row.legacy_ref.empty())) value.legacy_ref = row.legacy_ref;
    value.description = row.description;
    value.created_at = row.created_at;
    value.updated_at = row.updated_at;
    return value;
}

struct CfgImportJobRowWrite {
    std::string import_job_id;
    std::string device_id;
    std::string binding_id;
    std::string source_kind;
    std::string file_name;
    std::string file_sha256;
    int file_bytes;
    std::string address_base;
    std::string status;
    int row_total;
    int row_ok;
    int row_warning;
    int row_error;
    std::string idempotency_key;
    std::string submitted_by;
    std::string submitted_at;
    std::optional<std::string> finished_at;
    std::string error_code;
    std::string error_message;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgImportJobRowWrite*) { return "cfg_import_job"; }
YLT_REFL(CfgImportJobRowWrite, import_job_id, device_id, binding_id, source_kind, file_name,
         file_sha256, file_bytes, address_base, status, row_total, row_ok, row_warning, row_error,
         idempotency_key, submitted_by, submitted_at, finished_at, error_code, error_message)
inline const auto CfgImportJobRowWrite_key = ormpp::add_conflict_key_field(
    ylt::reflection::type_string<CfgImportJobRowWrite>(), "import_job_id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgImportJobRowWrite sql_row(const CfgImportJobRow& row) {
    CfgImportJobRowWrite value;
    value.import_job_id = row.import_job_id;
    value.device_id = row.device_id;
    value.binding_id = row.binding_id;
    value.source_kind = row.source_kind;
    value.file_name = row.file_name;
    value.file_sha256 = row.file_sha256;
    value.file_bytes = row.file_bytes;
    value.address_base = row.address_base;
    value.status = row.status;
    value.row_total = row.row_total;
    value.row_ok = row.row_ok;
    value.row_warning = row.row_warning;
    value.row_error = row.row_error;
    value.idempotency_key = row.idempotency_key;
    value.submitted_by = row.submitted_by;
    value.submitted_at = row.submitted_at;
    if (!(row.finished_at.empty())) value.finished_at = row.finished_at;
    value.error_code = row.error_code;
    value.error_message = row.error_message;
    return value;
}

struct CfgImportRowWrite {
    std::int64_t id;
    std::string import_job_id;
    int line_no;
    std::string vendor_key;
    std::string vendor_name;
    std::string point_type;
    std::string target_device_path;
    std::string semantic_code;
    std::optional<std::string> address_space;
    int address_offset;
    int bit_offset;
    int register_count;
    std::optional<std::string> protocol_address_json;
    std::string wire_type;
    std::string byte_order;
    std::string value_type;
    double scale_factor;
    double offset_value;
    std::string unit;
    std::string parse_status;
    std::string parse_code;
    std::string parse_message;
    std::string confirm_state;
    std::string confirm_note;
    std::string confirm_by;
    std::optional<std::string> confirm_at;
    std::string resolved_point_id;
};

/** @brief 返回写入表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CfgImportRowWrite*) { return "cfg_import_row"; }
YLT_REFL(CfgImportRowWrite, id, import_job_id, line_no, vendor_key, vendor_name, point_type,
         target_device_path, semantic_code, address_space, address_offset, bit_offset,
         register_count, protocol_address_json, wire_type, byte_order, value_type, scale_factor,
         offset_value, unit, parse_status, parse_code, parse_message, confirm_state, confirm_note,
         confirm_by, confirm_at, resolved_point_id)
inline const auto CfgImportRowWrite_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgImportRowWrite>(), "id");
inline const auto CfgImportRowWrite_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgImportRowWrite>(), "id");

/** @brief 转换可空写入行。 @param[in] row 公共记录。 @return SQL 写入记录。 */
inline CfgImportRowWrite sql_row(const CfgImportRow& row) {
    CfgImportRowWrite value;
    value.id = row.id;
    value.import_job_id = row.import_job_id;
    value.line_no = row.line_no;
    value.vendor_key = row.vendor_key;
    value.vendor_name = row.vendor_name;
    value.point_type = row.point_type;
    value.target_device_path = row.target_device_path;
    value.semantic_code = row.semantic_code;
    if (!(row.address_space.empty())) value.address_space = row.address_space;
    value.address_offset = row.address_offset;
    value.bit_offset = row.bit_offset;
    value.register_count = row.register_count;
    if (!(row.protocol_address_json.empty()))
        value.protocol_address_json = row.protocol_address_json;
    value.wire_type = row.wire_type;
    value.byte_order = row.byte_order;
    value.value_type = row.value_type;
    value.scale_factor = row.scale_factor;
    value.offset_value = row.offset_value;
    value.unit = row.unit;
    value.parse_status = row.parse_status;
    value.parse_code = row.parse_code;
    value.parse_message = row.parse_message;
    value.confirm_state = row.confirm_state;
    value.confirm_note = row.confirm_note;
    value.confirm_by = row.confirm_by;
    if (!(row.confirm_at.empty())) value.confirm_at = row.confirm_at;
    value.resolved_point_id = row.resolved_point_id;
    return value;
}
}  // namespace ems::database::detail
