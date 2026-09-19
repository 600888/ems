/**
 * @file logger.h
 * @brief 结构化日志基础能力（design.md 第 12.1 节）
 *
 * 结构化日志统一包含 process_name、instance_id、module、trace_id，
 * 以及适用的 station/unit/device/point、config_version、
 * event_id/command_id、error_code。密码、令牌、连接串凭据和原始
 * 认证载荷不输出。后台异步日志队列有界；业务审计不依赖调试日志。
 */
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ems::observability {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4
};

/// 结构化日志字段（值已脱敏）
struct LogField {
    std::string key;
    std::string value;
};

class ILogger {
public:
    virtual ~ILogger() = default;

    /// 输出一条结构化日志；fields 数量有上限（防滥用）
    virtual void log(LogLevel level, std::string_view module, std::string_view message,
                     std::initializer_list<LogField> fields = {}) = 0;
};

}  // namespace ems::observability
