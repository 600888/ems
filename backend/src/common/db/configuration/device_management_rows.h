/**
 * @file device_management_rows.h
 * @brief 设备管理行的 ormpp 映射（私有实现）：表名、字段反射、主键。
 *
 * 只在公共 MySQL 实现内部使用，不进入业务头文件。
 */
#pragma once

#include <common/database/configuration/device_management.h>

#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <ormpp/entity.hpp>
#include <string_view>

namespace ems::database {

/**
 * @brief 映射表名 cfg_device。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgDeviceRow*) { return "cfg_device"; }

/**
 * @brief 映射表名 cfg_device_type。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgDeviceTypeRow*) { return "cfg_device_type"; }

/**
 * @brief 映射表名 cfg_type_point。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgTypePointRow*) { return "cfg_type_point"; }

/**
 * @brief 映射表名 cfg_template。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgTemplateRow*) { return "cfg_template"; }

/**
 * @brief 映射表名 cfg_channel。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgChannelRow*) { return "cfg_channel"; }

/**
 * @brief 映射表名 cfg_binding。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgBindingRow*) { return "cfg_binding"; }

/**
 * @brief 映射表名 cfg_point_yc。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgPointYcRow*) { return "cfg_point_yc"; }

/**
 * @brief 映射表名 cfg_point_yx。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgPointYxRow*) { return "cfg_point_yx"; }

/**
 * @brief 映射表名 cfg_point_yk。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgPointYkRow*) { return "cfg_point_yk"; }

/**
 * @brief 映射表名 cfg_point_yt。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgPointYtRow*) { return "cfg_point_yt"; }

/**
 * @brief 映射表名 cfg_import_job。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgImportJobRow*) { return "cfg_import_job"; }

/**
 * @brief 映射表名 cfg_import_row。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(CfgImportRow*) { return "cfg_import_row"; }

/**
 * @brief 映射表名 ops_comm_stat_5m。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(OpsCommStat5mRow*) { return "ops_comm_stat_5m"; }

/**
 * @brief 映射表名 ops_state_log。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(OpsStateLogRow*) { return "ops_state_log"; }

// 字段顺序必须与 DDL 列顺序一致（SELECT * 按位置映射）

YLT_REFL(CfgDeviceRow, id, device_id, parent_device_id, node_kind, code, name, path, depth,
         sort_order, device_type_code, type_version, template_id, source_device_id, vendor, model,
         firmware_version, rated_power_kw, timezone, default_stale_after_ms, config_revision,
         enabled, description, legacy_ref, created_at, updated_at)

YLT_REFL(CfgDeviceTypeRow, type_code, type_version, name, category, status, description, created_at,
         updated_at)

YLT_REFL(CfgTypePointRow, type_code, type_version, semantic_code, name, point_type, value_type,
         unit, required, capability_codes_json, writable, stale_after_ms, sort_order)

YLT_REFL(CfgTemplateRow, template_id, template_version, device_type_code, type_version, vendor,
         model, status, points_json, description, created_at, updated_at)

YLT_REFL(CfgChannelRow, id, channel_id, code, name, protocol, conn_mode, endpoint, serial_json,
         poll_interval_ms, timeout_ms, retry_times, max_sessions, enabled, description, created_at,
         updated_at)

YLT_REFL(CfgBindingRow, id, binding_id, device_id, channel_id, device_address, profile_version,
         can_collect, can_control, can_forward, enabled, description, created_at, updated_at)

YLT_REFL(CfgPointYcRow, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, decimals, scale_factor,
         offset_value, range_min, range_max, deadband, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)

YLT_REFL(CfgPointYxRow, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, soe, invert, enum_json,
         unknown_policy, feedback_of_point_id, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)

YLT_REFL(CfgPointYkRow, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, write_mode,
         fixed_raw_value, pulse_width_ms, min_interval_ms, write_group_key, feedback_json,
         feedback_timeout_ms, failure_policy, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)

YLT_REFL(CfgPointYtRow, id, point_id, owner_device_id, binding_id, code, name, semantic_code,
         vendor_key, origin, address_space, address_offset, bit_offset, register_count,
         protocol_address_json, wire_type, byte_order, value_type, unit, decimals, scale_factor,
         offset_value, range_min, range_max, min_interval_ms, write_group_key, feedback_json,
         feedback_timeout_ms, failure_policy, enabled, disabled_reason, stale_after_ms,
         source_import_row_id, legacy_ref, description, created_at, updated_at)

YLT_REFL(CfgImportJobRow, import_job_id, device_id, binding_id, source_kind, file_name, file_sha256,
         file_bytes, address_base, status, row_total, row_ok, row_warning, row_error,
         idempotency_key, submitted_by, submitted_at, finished_at, error_code, error_message)

YLT_REFL(CfgImportRow, id, import_job_id, line_no, vendor_key, vendor_name, point_type,
         target_device_path, semantic_code, address_space, address_offset, bit_offset,
         register_count, protocol_address_json, wire_type, byte_order, value_type, scale_factor,
         offset_value, unit, parse_status, parse_code, parse_message, confirm_state, confirm_note,
         confirm_by, confirm_at, resolved_point_id)

YLT_REFL(OpsCommStat5mRow, binding_id, window_start, request_total, success_total, timeout_total,
         error_total, avg_latency_ms, p95_latency_ms, last_valid_at, last_error_code)

YLT_REFL(OpsStateLogRow, id, object_kind, object_id, state, since, reason)

// 自增主键：插入时跳过该列，由 MySQL 生成

inline const auto cfg_device_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgDeviceRow>(), "id");
inline const auto cfg_channel_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgChannelRow>(), "id");
inline const auto cfg_binding_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgBindingRow>(), "id");
inline const auto cfg_point_yc_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYcRow>(), "id");
inline const auto cfg_point_yx_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYxRow>(), "id");
inline const auto cfg_point_yk_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYkRow>(), "id");
inline const auto cfg_point_yt_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgPointYtRow>(), "id");
inline const auto cfg_import_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CfgImportRow>(), "id");
inline const auto ops_state_log_row_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<OpsStateLogRow>(), "id");

// 更新时按稳定业务 ID 定位（不是数字主键）

inline const auto cfg_device_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgDeviceRow>(), "device_id");
inline const auto cfg_channel_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgChannelRow>(), "channel_id");
inline const auto cfg_binding_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgBindingRow>(), "binding_id");
inline const auto cfg_point_yc_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYcRow>(), "point_id");
inline const auto cfg_point_yx_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYxRow>(), "point_id");
inline const auto cfg_point_yk_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYkRow>(), "point_id");
inline const auto cfg_point_yt_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgPointYtRow>(), "point_id");
inline const auto cfg_import_job_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgImportJobRow>(), "import_job_id");
inline const auto cfg_import_row_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CfgImportRow>(), "id");

}  // namespace ems::database
