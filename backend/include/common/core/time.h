/**
 * @file time.h
 * @brief 业务时间基础类型（docs/common/design.md 第 5.3 节）
 *
 * 业务时间统一为 UTC Unix 微秒 int64。持久事件分别保存源时间与
 * 接收时间；源时间缺失时采用接收时间并保留缺失原因。进程内超时
 * 使用 steady clock（见 call_context.h），序列化只传业务时间。
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>

namespace ems::core {

/// 业务时间：UTC Unix 微秒
using UtcMicros = std::int64_t;

/// 无效时间哨兵（源时间缺失等场景）
constexpr UtcMicros kInvalidUtcMicros = std::numeric_limits<std::int64_t>::min();

inline bool is_valid_time(UtcMicros t) noexcept { return t != kInvalidUtcMicros; }

inline UtcMicros to_utc_micros(const std::chrono::system_clock::time_point& tp) noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

inline std::chrono::system_clock::time_point from_utc_micros(UtcMicros t) noexcept {
    return std::chrono::system_clock::time_point(std::chrono::microseconds(t));
}

inline UtcMicros now_utc_micros() noexcept {
    return to_utc_micros(std::chrono::system_clock::now());
}

/**
 * @brief 格式化为 RFC3339 UTC 文本（秒精度 + 固定 .mmm 毫秒）
 * @note  仅用于日志/诊断输出；契约传输使用 int64 微秒本身
 */
inline std::string format_rfc3339_utc(UtcMicros t) {
    if (!is_valid_time(t)) {
        return "invalid_time";
    }
    const std::time_t seconds = static_cast<std::time_t>(t / 1000000);
    const int millis = static_cast<int>((t % 1000000) / 1000);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &seconds);
#else
    gmtime_r(&seconds, &tm_utc);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, millis);
    return std::string(buf);
}

}  // namespace ems::core
