#pragma once
#include <common/database/configuration/repository.h>

#include <array>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <ormpp/entity.hpp>
#include <string_view>

namespace ems::database {
/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(DeviceRecord*) { return "device"; }

/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(ChannelRecord*) { return "channel"; }

/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(AnalogPoint*) { return "point_yc"; }

/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(StatusPoint*) { return "point_yx"; }

/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(ControlPoint*) { return "point_yk"; }

/**
 * @brief 映射私有 ORM 表名。
 * @return 固定数据库表名。
 */
constexpr std::string_view get_alias_struct_name(SetpointPoint*) { return "point_yt"; }

YLT_REFL(DeviceRecord, id, code, name, device_type, group_id, description, enable, created_at,
         updated_at);

YLT_REFL(ChannelRecord, id, code, name, device_id, protocol_type, conn_type, ip, port, com_port,
         baud_rate, data_bits, stop_bits, parity, rtu_addr, timeout, enable, created_at,
         updated_at);

YLT_REFL(AnalogPoint, id, code, name, channel_id, device_id, protocol_type, func_code, slave_id,
         reg_addr, bit_position, data_id, casdu, ioa, object_ref, decode_code, endian, iec104_type,
         mul_coe, add_coe, min_limit, max_limit, unit, enable, description, created_at, updated_at);

YLT_REFL(StatusPoint, id, code, name, channel_id, device_id, protocol_type, func_code, slave_id,
         reg_addr, bit_position, data_id, casdu, ioa, object_ref, decode_code, endian, iec104_type,
         bit, reverse_flag, enable, description, created_at, updated_at);

YLT_REFL(ControlPoint, id, code, name, channel_id, device_id, protocol_type, func_code, slave_id,
         reg_addr, bit_position, data_id, casdu, ioa, object_ref, decode_code, endian, iec104_type,
         bit, command_type, related_yx_id, enable, description, created_at, updated_at);

YLT_REFL(SetpointPoint, id, code, name, channel_id, device_id, protocol_type, func_code, slave_id,
         reg_addr, bit_position, data_id, casdu, ioa, object_ref, decode_code, endian, iec104_type,
         related_yc_id, mul_coe, add_coe, min_limit, max_limit, unit, enable, description,
         created_at, updated_at);

}  // namespace ems::database

namespace ems::database {
inline const auto device_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<DeviceRecord>(), "id");
inline const auto channel_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<ChannelRecord>(), "id");
inline const auto point_yc_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<AnalogPoint>(), "id");
inline const auto point_yx_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<StatusPoint>(), "id");
inline const auto point_yk_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<ControlPoint>(), "id");
inline const auto point_yt_auto_key =
    ormpp::add_auto_key_field(ylt::reflection::type_string<SetpointPoint>(), "id");
}  // namespace ems::database
