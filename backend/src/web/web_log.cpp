#include "web_log.h"

#include <trantor/utils/Logger.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include "spdlog/spdlog.h"

namespace ems::web {

void configure_framework_logging() {
    using LV = spdlog::level::level_enum;
    const auto logger = spdlog::default_logger();
    const auto level = logger->level();
    trantor::Logger::setLogLevel(static_cast<trantor::Logger::LogLevel>(
        level == LV::off ? trantor::Logger::kNumberOfLogLevels : static_cast<int>(level)));
    trantor::Logger::setDisplayLocalTime(true);
    trantor::Logger::setOutputFunction(
        [logger](const char* data, std::uint64_t length) {
            std::string_view message(data, static_cast<std::size_t>(length));
            if (!message.empty() && message.back() == '\n') message.remove_suffix(1);

            // Trantor 回调只提供完整文本，从时间、线程后的等级字段恢复日志等级。
            std::istringstream prefix{std::string(message)};
            std::string date, time, thread, severity;
            prefix >> date >> time >> thread >> severity;
            static const std::unordered_map<std::string, LV> levels{
                {"TRACE", LV::trace}, {"DEBUG", LV::debug}, {"INFO", LV::info},
                {"WARN", LV::warn},   {"ERROR", LV::err},   {"FATAL", LV::critical}};
            const auto found = levels.find(severity);
            logger->log(found == levels.end() ? LV::info : found->second, "{}", message);
        },
        [logger] { logger->flush(); });
}

}  // namespace ems::web
