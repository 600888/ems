#pragma once
#include "util/log_base.h"

static constexpr const char* LOGGER_NAME = "net";

// 通道报文日志
class ChannelMsgLogger {
    ChannelMsgLogger() = delete;
    ChannelMsgLogger(const ChannelMsgLogger&) = delete;

   public:
    static inline std::string channelMsgLoggerName(const std::string& channelCode) noexcept {
        return std::string("msg-") + channelCode;
    }

    static inline std::shared_ptr<spdlog::logger> addChannelLogger(const std::string& channelCode,
                                                                   LV log_level) noexcept {
        try {
            std::string logFilePath =
                logPath() + LOGGER_NAME + "/channels/" + channelCode + "/" + channelCode;
#ifdef DEBUG
            logFilePath += "-debug";
#endif
            logFilePath += ".log";
            std::vector<spdlog::sink_ptr> sinks;
            auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFilePath, 1024 * 1024 * 10, 10);
            sinks.push_back(rotating_sink);

            std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::async_logger>(
                channelMsgLoggerName(channelCode), sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            spdlog::register_logger(logger);
            logger->set_level(log_level);
            // 设置日志格式[年-月-日 时:分:秒.毫秒][进程号:线程号][调试级别](宏定义中添加: [文件名:行号]<函数名> - 具体内容)
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e]%v");

            // 设置当出发debug或更严重的错误时立刻刷新日志到disk
            logger->flush_on(spdlog::level::debug);

            return logger;
        } catch (...) {
            spdlog::drop_all();
        }

        return nullptr;
    }

    static inline std::shared_ptr<spdlog::logger> getChannelMsgLogger(
        const std::string channelCode) noexcept {
        try {
            auto logger = spdlog::get(channelMsgLoggerName(channelCode));
            if (logger == nullptr) {
                logger = addChannelLogger(channelCode, LV::debug);
            }

            return logger;
        } catch (...) {
            spdlog::drop_all();
        }

        return nullptr;
    }

    template <typename... Args>
    static inline void logChannelMsg(const std::string& channelCode, const char* format,
                                     const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->info(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelTrace(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->trace(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelDebug(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->debug(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelInfo(const std::string& channelCode, const char* format,
                                      const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->info(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelWarn(const std::string& channelCode, const char* format,
                                      const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->warn(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelError(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->error(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }

    template <typename... Args>
    static inline void logChannelCritical(const std::string& channelCode, const char* format,
                                          const Args&... args) noexcept {
        try {
            auto logger = getChannelMsgLogger(channelCode);
            logger->critical(format, args...);
        } catch (...) {
            spdlog::drop_all();
        }
    }
};

// 通道日志宏（通过通道对象指针，需有 chanCode() 方法）
#define CHANNEL_LOG_TRACE(ch, format, ...) \
    (ChannelMsgLogger::logChannelTrace((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_DEBUG(ch, format, ...) \
    (ChannelMsgLogger::logChannelDebug((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_INFO(ch, format, ...) \
    (ChannelMsgLogger::logChannelInfo((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_WARN(ch, format, ...) \
    (ChannelMsgLogger::logChannelWarn((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_ERROR(ch, format, ...) \
    (ChannelMsgLogger::logChannelError((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_CRITICAL(ch, format, ...) \
    (ChannelMsgLogger::logChannelCritical((ch)->chanCode(), FORMATTED(format), ##__VA_ARGS__))

// 通道日志宏（直接使用通道编码字符串）
#define CHANNEL_LOG_TRACE_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelTrace(code, FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_DEBUG_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelDebug(code, FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_INFO_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelInfo(code, FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_WARN_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelWarn(code, FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_ERROR_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelError(code, FORMATTED(format), ##__VA_ARGS__))
#define CHANNEL_LOG_CRITICAL_CODE(code, format, ...) \
    (ChannelMsgLogger::logChannelCritical(code, FORMATTED(format), ##__VA_ARGS__))

inline LogInitializer logInitializer(LOGGER_NAME, LV::debug, false, 4);  // 改为 CPU 核心个数