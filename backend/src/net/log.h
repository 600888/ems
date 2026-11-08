#pragma once

#include <mutex>

#include "log/log_base.h"

static constexpr const char* LOGGER_NAME = "net";

/** @brief 通道报文日志；进程入口须先创建 ProcessLogger，初始化默认日志器及异步线程池。 */
class ChannelMsgLogger {
    /** @brief 禁止复制或移动该对象。 */
    ChannelMsgLogger() = delete;
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    ChannelMsgLogger(const ChannelMsgLogger& other) = delete;

    /**
     * @brief 将通道名称编码为不依赖系统代码页的单个日志路径分量。
     * @param[in] channelCode UTF-8 通道名称。
     * @return ASCII 路径分量；非 ASCII 字节及特殊字符使用百分号编码。
     */
    static inline std::string fileComponent(const std::string& channelCode) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string result;
        for (const unsigned char byte : channelCode) {
            if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == ' ') {
                result += static_cast<char>(byte);
            } else {
                result += '%';
                result += hex[byte >> 4];
                result += hex[byte & 15];
            }
        }
        return result.empty() ? "%00" : result;
    }

    /**
     * @brief 写入通道日志；日志不可用或格式错误时保留进程及其他通道的日志器。
     * @param[in] channelCode 通道名称。
     * @param[in] level 消息等级。
     * @param[in] format 运行时格式字符串。
     * @param[in] args 格式化参数。
     */
    template <typename... Args>
    static inline void logChannel(const std::string& channelCode, LV level, const char* format,
                                  const Args&... args) noexcept {
        try {
            if (auto logger = getChannelMsgLogger(channelCode))
                logger->log(level, fmt::vformat(format, fmt::make_format_args(args...)));
        } catch (...) {
            // 日志失败不能影响采集线程或注销其他日志器。
        }
    }

   public:
    /**
     * @brief 生成通道报文日志器名称。
     * @param[in] channelCode 通道编码。
     * @return 对应的文本内容。
     */
    static inline std::string channelMsgLoggerName(const std::string& channelCode) noexcept {
        return std::string("msg-") + channelCode;
    }

    /**
     * @brief 创建并注册通道报文日志器。
     * @param[in] channelCode 通道编码。
     * @param[in] log_level 日志级别。
     * @return 通道日志器；创建失败时回退到进程默认日志器，默认日志器缺失时为空。
     */
    static inline std::shared_ptr<spdlog::logger> addChannelLogger(const std::string& channelCode,
                                                                   LV log_level) noexcept {
        try {
            static std::mutex creation_mutex;
            std::lock_guard<std::mutex> lock(creation_mutex);
            const auto name = channelMsgLoggerName(channelCode);
            if (auto existing = spdlog::get(name)) return existing;
            const auto component = fileComponent(channelCode);
            std::string logFilePath =
                logPath() + LOGGER_NAME + "/channels/" + component + "/" + component;
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
            logger->set_level(log_level);
            // 设置日志格式[年-月-日 时:分:秒.毫秒][进程号:线程号][调试级别](宏定义中添加: [文件名:行号]<函数名> - 具体内容)
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e]%v");

            // 设置当出发debug或更严重的错误时立刻刷新日志到disk
            logger->flush_on(spdlog::level::debug);
            spdlog::register_logger(logger);

            return logger;
        } catch (const std::exception& error) {
            DefaultLogger::warn("Channel log unavailable for '{}': {}", channelCode, error.what());
        } catch (...) {
        }

        return spdlog::default_logger();
    }

    /**
     * @brief 获取通道报文日志器，必要时创建。
     * @param[in] channelCode 通道编码。
     * @return 通道日志器；创建失败时回退到进程默认日志器，默认日志器缺失时为空。
     */
    static inline std::shared_ptr<spdlog::logger> getChannelMsgLogger(
        const std::string& channelCode) noexcept {
        try {
            auto logger = spdlog::get(channelMsgLoggerName(channelCode));
            if (logger == nullptr) {
                logger = addChannelLogger(channelCode, LV::debug);
            }

            return logger;
        } catch (...) {
        }

        return spdlog::default_logger();
    }

    /**
     * @brief 向通道报文日志写入Msg级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelMsg(const std::string& channelCode, const char* format,
                                     const Args&... args) noexcept {
        logChannel(channelCode, LV::info, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Trace级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelTrace(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        logChannel(channelCode, LV::trace, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Debug级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelDebug(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        logChannel(channelCode, LV::debug, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Info级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelInfo(const std::string& channelCode, const char* format,
                                      const Args&... args) noexcept {
        logChannel(channelCode, LV::info, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Warn级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelWarn(const std::string& channelCode, const char* format,
                                      const Args&... args) noexcept {
        logChannel(channelCode, LV::warn, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Error级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelError(const std::string& channelCode, const char* format,
                                       const Args&... args) noexcept {
        logChannel(channelCode, LV::err, format, args...);
    }

    /**
     * @brief 向通道报文日志写入Critical级别消息。
     * @param[in] channelCode 通道编码。
     * @param[in] format 日志格式字符串。
     * @param[in] args 日志格式化参数。
     */
    template <typename... Args>
    static inline void logChannelCritical(const std::string& channelCode, const char* format,
                                          const Args&... args) noexcept {
        logChannel(channelCode, LV::critical, format, args...);
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
