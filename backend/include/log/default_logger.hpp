#pragma once
#include "default_log_format.h"
#include "spdlog/spdlog.h"

class DefaultLogger {
    /** @brief 禁止实例化日志工具类。 */
    DefaultLogger() = delete;

    /**
     * @brief 禁止复制日志工具类。
     * @param[in] other 待复制或移动的源对象。
     */
    DefaultLogger(const DefaultLogger& other) = delete;

   public:
    /**
     * @brief 输出 trace 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void trace(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->trace(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    /**
     * @brief 输出 debug 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void debug(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->debug(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    /**
     * @brief 输出 info 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void info(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->info(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    /**
     * @brief 输出 warn 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void warn(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->warn(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    /**
     * @brief 输出 error 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void error(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->error(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    /**
     * @brief 输出 critical 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void critical(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->critical(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }
};

#define LOG_TRACE(format, ...) (DefaultLogger::trace(FORMATTED(format), ##__VA_ARGS__))
#define LOG_DEBUG(format, ...) (DefaultLogger::debug(FORMATTED(format), ##__VA_ARGS__))
#define LOG_INFO(format, ...) (DefaultLogger::info(FORMATTED(format), ##__VA_ARGS__))
#define LOG_WARN(format, ...) (DefaultLogger::warn(FORMATTED(format), ##__VA_ARGS__))
#define LOG_ERROR(format, ...) (DefaultLogger::error(FORMATTED(format), ##__VA_ARGS__))
#define LOG_CRITICAL(format, ...) (DefaultLogger::critical(FORMATTED(format), ##__VA_ARGS__))
#define LOG_SEPARATOR(content) \
    (DefaultLogger::info("---------------------------{}---------------------------", content))
