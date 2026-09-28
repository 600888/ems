/**
 * @file event.h
 * @brief 事件信封与事件类型（design.md 第 10.1 节）
 *
 * EventEnvelope 至少包含 schema_version、event_type、event_id、trace_id、
 * producer_id、station_id/code、unit_id/code、asset_id、device_path、
 * config_version、event_time、ingest_time。station 事件的 unit 字段为 null，
 * 根事件 device_path 为 []；各事件 schema 明确允许为空的身份字段。
 *
 * 历史采样等保留事件使用版本化 JSON；调用方传逻辑 topic/scope。
 * 命令下发通过独立的 ZeroMQ 命令通道，不能把此事件类型直接发到 PUB/SUB。
 */
#pragma once

#include <common/core/identity.h>
#include <common/core/time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ems::messaging {

/// 事件类别（design.md 10.1 表）
enum class EventType : std::uint8_t {
    Telemetry = 0,      ///< 遥测样本
    ControlIntent = 1,  ///< 控制意图
    Command = 2,        ///< 控制命令（仅策略产生）
    CommandResult = 3,  ///< 命令回执
    AlarmEvent = 4,     ///< 告警事件
    ConfigChanged = 5,  ///< 配置版本变化（发布）
    ConfigApplied = 6   ///< 配置应用结果
    // 已移除事件的编号 7 保留，不复用。
};

/**
 * @brief 返回事件类型的稳定诊断名称。
 * @param[in] t 待转换的枚举值。
 * @return 对应对象的指针。
 */
inline const char* to_string(EventType t) noexcept {
    switch (t) {
        case EventType::Telemetry:
            return "Telemetry";
        case EventType::ControlIntent:
            return "ControlIntent";
        case EventType::Command:
            return "Command";
        case EventType::CommandResult:
            return "CommandResult";
        case EventType::AlarmEvent:
            return "AlarmEvent";
        case EventType::ConfigChanged:
            return "ConfigChanged";
        case EventType::ConfigApplied:
            return "ConfigApplied";
    }
    return "Unknown";
}

/// 事件信封（跨语言契约；载荷为版本化 JSON 文本）
struct EventEnvelope {
    std::string schema_version;  ///< 载荷 schema 版本
    EventType event_type{EventType::Telemetry};

    core::EventId event_id;  ///< 持久受理前生成，重试保持不变
    std::string trace_id;
    std::string producer_id;  ///< 生产者实例标识

    std::optional<core::StationId> station_id;  ///< 允许为空（按事件 schema）
    std::optional<core::EssUnitId> unit_id;     ///< station 事件为 null
    std::optional<core::AssetId> asset_id;      ///< 允许为空（按事件 schema）
    std::vector<std::string> device_path;       ///< 根事件为 []

    std::string config_version;                            ///< 产生事件时的配置版本
    core::UtcMicros event_time{core::kInvalidUtcMicros};   ///< 源时间
    core::UtcMicros ingest_time{core::kInvalidUtcMicros};  ///< 接收时间

    /// 版本化 JSON 载荷（各事件 schema 约束专属字段；
    /// 64 位整数/ID 在 JSON 中使用十进制字符串）
    std::string payload_json;
};

}  // namespace ems::messaging
