#pragma once

#include <common/database/alarm/repositories.h>

namespace ems::database::test {

/**
 * @brief 构造带完整状态的发生和恢复事件，时间相同以验证事件级去重。
 * @param[in] station 仅供测试使用的 ASCII 站 ID。
 * @param[in] time Unix 微秒。
 * @param[in] recovered 是否构造恢复事件。
 * @return 可由时序告警写入端口持久化的记录。
 */
inline AlarmArchiveItem alarm_item(const std::string& station, std::int64_t time,
                                   bool recovered = false) {
    const auto kind = recovered ? model::AlarmLifecycle::Cleared : model::AlarmLifecycle::Occurred;
    const std::string identity =
        R"("alarm_id":"alarm-1","device_id":"device-1","station_id":")" + station + R"(")";
    const auto event = recovered ? "event-2" : "event-1";
    const auto timestamp = std::to_string(time);
    return {core::EventId(event),
            model::AlarmId("alarm-1"),
            core::StationId(station),
            core::DeviceId("device-1"),
            kind,
            time,
            recovered ? 2 : 1,
            "{" + identity + R"(,"event_id":")" + event + R"(","kind":")" + model::to_string(kind) +
                R"(","event_time_us":)" + timestamp + "}",
            "{" + identity + R"(,"name":"过温告警","severity":"Critical","occurred_at_us":)" +
                timestamp + R"(,"cleared_at_us":)" + (recovered ? timestamp : "null") +
                R"(,"acknowledgement":null})"};
}

}  // namespace ems::database::test
