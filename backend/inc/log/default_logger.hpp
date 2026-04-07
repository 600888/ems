#pragma once
#include "spdlog/spdlog.h"
#include "default_log_format.h"

class DefaultLogger {
    DefaultLogger() = delete;
    DefaultLogger(const DefaultLogger&) = delete;

public:
    template <typename... Args>
    static inline void trace(const char* format, const Args&... args) noexcept
    {
        try {
            spdlog::default_logger()->trace(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void debug(const char* format, const Args&... args) noexcept
    {
        try {
            spdlog::default_logger()->debug(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void info(const char* format, const Args&... args) noexcept
    {
        try {
            spdlog::default_logger()->info(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void warn(const char* format, const Args&... args) noexcept
    {
        try {
            spdlog::default_logger()->warn(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void error(const char* format, const Args&... args) noexcept
    {
        try {
            spdlog::default_logger()->error(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void critical(const char* format, const Args&... args) noexcept
    {
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
#define LOG_SEPARATOR(content) (DefaultLogger::info("---------------------------{}---------------------------", content))
