#include <trantor/utils/Logger.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

#include "spdlog/sinks/ostream_sink.h"
#include "spdlog/spdlog.h"
#include "web/web_log.h"

int main() {
    try {
        for (const auto level : {spdlog::level::trace, spdlog::level::warn, spdlog::level::off}) {
            std::ostringstream output;
            auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
            auto logger = std::make_shared<spdlog::logger>("ems-web", sink);
            logger->set_level(level);
            logger->set_pattern("%l %v");
            spdlog::set_default_logger(logger);
            ems::web::configure_framework_logging();
            // Release 下 Trantor 的 LOG_TRACE 宏被编译掉，直接构造以验证转接等级。
            trantor::Logger(__FILE__, __LINE__, trantor::Logger::kTrace).stream() << "trace marker";
            LOG_DEBUG << "debug marker";
            LOG_INFO << "info marker";
            LOG_WARN << "warn marker {}";
            LOG_ERROR << "error marker";
            logger->flush();
            const auto text = output.str();
            const bool verbose = level == spdlog::level::trace;
            const bool enabled = level != spdlog::level::off;
            if ((text.find("trace marker") != std::string::npos) != verbose ||
                (text.find("debug marker") != std::string::npos) != verbose ||
                (text.find("info marker") != std::string::npos) != verbose ||
                (text.find("warn marker {}") != std::string::npos) != enabled ||
                (text.find("error marker") != std::string::npos) != enabled ||
                (enabled && (text.find("warning ") == std::string::npos ||
                             text.find("error ") == std::string::npos))) {
                throw std::runtime_error("framework forwarding failed at level " +
                                         std::to_string(static_cast<int>(level)) + ":\n" + text);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
