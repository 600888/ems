#pragma once

#include <cstdint>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <optional>
#include <ormpp/entity.hpp>
#include <string>
#include <string_view>

namespace ems::database::detail {

struct AlarmControlRow {
    std::int64_t id{0};
    std::string alarm_id, station_id, device_id;
    std::optional<std::string> active_key;
    std::int64_t occurred_at_us{0};
    std::optional<std::int64_t> cleared_at_us, acknowledged_at_us;
    std::string state_json;
};

struct AlarmOutboxRow {
    std::int64_t id{0};
    std::string event_id, alarm_id, station_id, device_id;
    std::int64_t stream_seq{0};
    std::int32_t kind{0};
    std::int64_t event_time_us{0};
    std::string event_json, state_json;
};

struct AlarmProgressRow {
    std::int64_t id{0};
    std::string station_id;
    std::int64_t committed_seq{0}, archived_seq{0};
};

/** @brief 返回当前告警表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(AlarmControlRow*) { return "alarm_control"; }

/** @brief 返回告警归档队列表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(AlarmOutboxRow*) { return "alarm_outbox"; }

/** @brief 返回告警归档进度表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(AlarmProgressRow*) {
    return "alarm_archive_progress";
}

YLT_REFL(AlarmControlRow, id, alarm_id, station_id, device_id, active_key, occurred_at_us,
         cleared_at_us, acknowledged_at_us, state_json)
YLT_REFL(AlarmOutboxRow, id, event_id, alarm_id, station_id, device_id, stream_seq, kind,
         event_time_us, event_json, state_json)
YLT_REFL(AlarmProgressRow, id, station_id, committed_seq, archived_seq)

inline const auto alarm_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<AlarmControlRow>(), "id");
inline const auto alarm_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<AlarmControlRow>(), "id");
inline const auto alarm_outbox_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<AlarmOutboxRow>(), "id");
inline const auto alarm_progress_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<AlarmProgressRow>(), "id");
inline const auto alarm_progress_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<AlarmProgressRow>(), "id");

}  // namespace ems::database::detail
