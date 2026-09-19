/**
 * @file health.h
 * @brief 健康状态基础类型（design.md 第 12.1/12.2 节）
 *
 * 健康分 liveness、readiness、degraded，不能仅以线程存活宣称就绪。
 * 诊断必须区分：设备离线、配置未应用、实时后端不可用、历史积压、
 * 控制链路不就绪。公共模块返回组件健康，进程决定最终业务就绪。
 */
#pragma once

#include <string>

namespace ems::observability {

/// 组件健康状态
enum class HealthState : std::uint8_t {
    Ready    = 0,  ///< 就绪
    Degraded = 1,  ///< 降级可用
    NotReady = 2,  ///< 未就绪（不能承接新业务）
    Failed   = 3   ///< 故障
};

inline const char* to_string(HealthState s) noexcept {
    switch (s) {
        case HealthState::Ready:    return "Ready";
        case HealthState::Degraded: return "Degraded";
        case HealthState::NotReady: return "NotReady";
        case HealthState::Failed:   return "Failed";
    }
    return "Unknown";
}

/// 组件健康报告
struct ComponentHealth {
    std::string component;   ///< 组件名（如 realtime.redis / config.apply / messaging）
    HealthState state{HealthState::NotReady};
    std::string detail;      ///< 区分原因：设备离线/配置未应用/后端不可用/积压等
};

}  // namespace ems::observability
