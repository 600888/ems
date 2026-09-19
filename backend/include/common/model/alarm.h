/**
 * @file alarm.h
 * @brief 告警定义与告警事件（design.md 第 6.1 节）
 *
 * 告警定义、告警事件数据类型可共享；告警产生/恢复/确认的业务
 * 状态机归策略模块。
 */
#pragma once

#include <string>

#include <common/core/identity.h>
#include <common/core/time.h>

namespace ems::model {

/// 告警级别
enum class AlarmSeverity : std::uint8_t {
    Info     = 0,
    Warning  = 1,
    Critical = 2
};

/// 告警生命周期事件（状态机在策略模块，这里只描述事件事实）
enum class AlarmLifecycle : std::uint8_t {
    Occurred      = 0,  ///< 发生
    Cleared       = 1,  ///< 恢复
    Acknowledged  = 2   ///< 确认
};

/// 告警定义（版本化规则引用）
struct AlarmDefinition {
    std::string alarm_code;    ///< 稳定告警编码
    std::string name;
    AlarmSeverity severity{AlarmSeverity::Warning};
    std::string capability_code;  ///< 关联能力（可选）
    std::string rule_ref;         ///< 规则引用（规则引擎在策略模块）
    bool enabled{true};
};

/// 告警事件
struct AlarmEvent {
    core::EventId event_id;      ///< 持久受理前生成
    std::string alarm_code;      ///< 规则/定义引用
    core::AssetId asset_id;
    AlarmLifecycle lifecycle{AlarmLifecycle::Occurred};
    std::string acknowledged_by;  ///< 确认者（lifecycle == Acknowledged 时有效）
    std::string detail;           ///< 脱敏详情
    core::UtcMicros event_time{core::kInvalidUtcMicros};
    core::UtcMicros ingest_time{core::kInvalidUtcMicros};
};

inline const char* to_string(AlarmSeverity s) noexcept {
    switch (s) {
        case AlarmSeverity::Info:     return "Info";
        case AlarmSeverity::Warning:  return "Warning";
        case AlarmSeverity::Critical: return "Critical";
    }
    return "Unknown";
}

inline const char* to_string(AlarmLifecycle l) noexcept {
    switch (l) {
        case AlarmLifecycle::Occurred:     return "Occurred";
        case AlarmLifecycle::Cleared:      return "Cleared";
        case AlarmLifecycle::Acknowledged: return "Acknowledged";
    }
    return "Unknown";
}

}  // namespace ems::model
