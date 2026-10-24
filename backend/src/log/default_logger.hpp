#pragma once
#include <string>

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

    /**
     * @brief 用运行时格式串格式化日志内容。
     *
     * spdlog 1.16 起格式串按编译期常量校验（fmt::format_string），而 LOG_* 宏会把
     * 字面量与文件名、行号拼成运行期字符串，直接传给 spdlog 无法通过编译。
     * 这里先用 fmt::vformat 完成格式化，再把结果作为普通消息交给日志器。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     * @return 格式化后的文本；格式串与参数不匹配时抛出异常。
     */
    template <typename... Args>
    static inline std::string format_message(const char* format, const Args&... args) {
        return fmt::vformat(format, fmt::make_format_args(args...));
    }

   public:
    /**
     * @brief 输出 trace 日志；异常时清理默认 logger。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void trace(const char* format, const Args&... args) noexcept {
        try {
            spdlog::default_logger()->log(spdlog::level::trace, format_message(format, args...));
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
            spdlog::default_logger()->log(spdlog::level::debug, format_message(format, args...));
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
            spdlog::default_logger()->log(spdlog::level::info, format_message(format, args...));
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
            spdlog::default_logger()->log(spdlog::level::warn, format_message(format, args...));
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
            spdlog::default_logger()->log(spdlog::level::err, format_message(format, args...));
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
            spdlog::default_logger()->log(spdlog::level::critical, format_message(format, args...));
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
