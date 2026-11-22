#pragma once

#include <common/database/timeseries/store.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace ems::database::detail {
constexpr std::size_t kMaxAlarmPayload = 50000;

/** @brief 校验时序读取条件。 @param[in] query 固定归档查询。
 * @return 条件有效时为 true。 */
inline bool valid_alarm_query(const AlarmArchiveQuery& query) {
    return !query.station_id.empty() && query.station_id.value().size() <= 64 &&
           query.alarm_id.value().size() <= 64 && query.archived_seq >= 0 &&
           (!query.alarm_id.empty() ||
            (!query.events && query.start_us >= 0 && query.end_us >= query.start_us &&
             query.end_us <= kMaxAlarmArchiveTimeUs));
}

/** @brief 获取查询预算。 @param[in] query 已校验条件。 @return 最大有效行数。 */
inline std::uint32_t alarm_read_budget(const AlarmArchiveQuery& query) {
    return query.alarm_id.empty() ? kMaxArchivedAlarmRows : 3;
}

/** @brief 获取原始行并核对归档身份。 @param[in] query 已校验条件。
 * @param[in] seq 时序顺序。 @param[in] payload 时序原始 JSON。
 * @return 公共行或数据损坏错误。 */
inline core::Result<ArchivedAlarmRow> read_alarm_row(const AlarmArchiveQuery& query,
                                                     std::int64_t seq, std::string payload) {
    const auto invalid =
        core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                         "invalid archived alarm payload");
    if (seq <= 0 || seq > query.archived_seq || payload.size() > kMaxAlarmPayload) return invalid;
    try {
        const auto value = nlohmann::json::parse(payload);
        auto alarm_id = value.at("alarm_id").get<std::string>();
        auto device_id = value.at("device_id").get<std::string>();
        if (value.at("station_id") != query.station_id.value() || alarm_id.empty() ||
            device_id.empty() || (!query.alarm_id.empty() && alarm_id != query.alarm_id.value()))
            return invalid;
        if (query.alarm_id.empty()) {
            const auto& occurred = value.at("occurred_at_us");
            if (!occurred.is_number_integer() || occurred < 0 ||
                (occurred.is_number_unsigned()
                     ? occurred.get<std::uint64_t>() > static_cast<std::uint64_t>(query.end_us)
                     : occurred.get<std::int64_t>() > query.end_us) ||
                occurred < query.start_us)
                return invalid;
        }
        return ArchivedAlarmRow{model::AlarmId(std::move(alarm_id)),
                                core::DeviceId(std::move(device_id)), seq, std::move(payload)};
    } catch (const nlohmann::json::exception&) {
        return invalid;
    }
}

struct AlarmStateColumns {
    std::int64_t occurred_at_us{0};
    std::int64_t cleared_at_us{-1};       ///< -1 表示未恢复。
    std::int64_t acknowledged_at_us{-1};  ///< -1 表示未确认。
    std::string severity;
};

/**
 * @brief 校验归档行的关联身份并提取时序筛选字段。
 * @param[in] item 告警仓储提供的事件和对应状态。
 * @return 状态列；持久数据无法解析或不一致时返回 CorruptData。
 */
inline core::Result<AlarmStateColumns> alarm_state_columns(const AlarmArchiveItem& item) {
    using Json = nlohmann::json;
    const auto invalid = core::make_error(
        core::ErrorCode::CorruptData, core::WriteEffect::NotApplied, "invalid alarm archive row");
    if (item.stream_seq <= 0 || item.event_time_us < 0 ||
        item.event_time_us > kMaxAlarmArchiveTimeUs || item.event_json.size() > kMaxAlarmPayload ||
        item.state_json.size() > kMaxAlarmPayload)
        return invalid;

    const auto valid_id = [](const auto& id) {
        return !id.empty() && id.value().size() <= 64 && id.value().find('\0') == std::string::npos;
    };
    if (!valid_id(item.event_id) || !valid_id(item.alarm_id) || !valid_id(item.device_id) ||
        !valid_id(item.station_id))
        return invalid;

    const auto time = [](const Json& value) {
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number <= static_cast<std::uint64_t>(kMaxAlarmArchiveTimeUs))
                return static_cast<std::int64_t>(number);
        } else if (value.is_number_integer()) {
            const auto number = value.get<std::int64_t>();
            if (number >= 0 && number <= kMaxAlarmArchiveTimeUs) return number;
        }
        throw std::invalid_argument("alarm timestamp");
    };
    try {
        const auto event = Json::parse(item.event_json);
        const auto state = Json::parse(item.state_json);
        for (const auto* row : {&event, &state})
            if (row->at("alarm_id") != item.alarm_id.value() ||
                row->at("device_id") != item.device_id.value() ||
                row->at("station_id") != item.station_id.value())
                return invalid;
        const auto kind = model::to_string(item.kind);
        if (std::string_view(kind) == "Unknown" || event.at("event_id") != item.event_id.value() ||
            time(event.at("event_time_us")) != item.event_time_us || event.at("kind") != kind)
            return invalid;
        AlarmStateColumns columns;
        columns.occurred_at_us = time(state.at("occurred_at_us"));
        const auto& cleared = state.at("cleared_at_us");
        const auto& ack = state.at("acknowledgement");
        if (!cleared.is_null()) columns.cleared_at_us = time(cleared);
        if (!ack.is_null()) columns.acknowledged_at_us = time(ack.at("at_us"));
        columns.severity = state.at("severity").get<std::string>();
        if (columns.severity != "Info" && columns.severity != "Warning" &&
            columns.severity != "Critical")
            return invalid;
        if (item.event_time_us < columns.occurred_at_us ||
            (columns.cleared_at_us >= 0 && columns.cleared_at_us < columns.occurred_at_us) ||
            (columns.acknowledged_at_us >= 0 &&
             columns.acknowledged_at_us < columns.occurred_at_us))
            return invalid;
        switch (item.kind) {
            case model::AlarmLifecycle::Occurred:
                if (columns.occurred_at_us != item.event_time_us || !cleared.is_null() ||
                    !ack.is_null())
                    return invalid;
                break;
            case model::AlarmLifecycle::Cleared:
                if (columns.cleared_at_us != item.event_time_us) return invalid;
                break;
            case model::AlarmLifecycle::Acknowledged:
                if (columns.acknowledged_at_us != item.event_time_us) return invalid;
                break;
        }
        return columns;
    } catch (const Json::exception&) {
        return invalid;
    } catch (const std::invalid_argument&) {
        return invalid;
    }
}

/**
 * @brief 将业务 ID 无损编码为 SQL 标识符或 InfluxDB 标签允许的字符。
 * @param[in] value 原始 ID。
 * @return 小写十六进制文本。
 */
inline std::string alarm_id_hex(const std::string& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char ch : value) {
        encoded += digits[ch >> 4];
        encoded += digits[ch & 15];
    }
    return encoded;
}
}  // namespace ems::database::detail
