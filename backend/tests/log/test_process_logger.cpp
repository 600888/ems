#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"
#include "net/log.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify(const std::filesystem::path& root, const std::string& scenario) {
    require(spdlog::get("net") == nullptr, "net library initialized the process logger");
    const std::string name = "ems-log-test";
    auto expected_level = LV::info;
    if (scenario != "missing") {
        nlohmann::json modules = nlohmann::json::object();
        if (scenario == "partial") {
            modules["aaa"] = {{"level", "critical"}, {"file_size", 1}, {"count", 1}};
            modules[name] = nlohmann::json::object();
        } else if (scenario == "trace") {
            expected_level = LV::trace;
            modules[name] = {{"level", "trace"}};
        } else if (scenario == "off") {
            expected_level = LV::off;
            modules[name] = {{"level", "off"}};
        } else if (scenario == "rotation") {
            modules[name] = {{"level", "info"}, {"file_size", 1}, {"count", 2}};
        }
        std::ofstream config(root / "conf/log.json");
        config << (scenario == "malformed" ? "{invalid"
                                           : nlohmann::json{{"modules", modules}}.dump());
    }

    {
        const ProcessLogger process_logger(name);
        const auto logger = spdlog::get(name);
        require(logger != nullptr, "process logger was not registered");
        require(logger == spdlog::default_logger(), "process logger is not the default");
        require(logger->level() == expected_level, "configured or fallback level is incorrect");
        if (scenario == "channel_failure") {
            const auto channels = root / "log/net/channels";
            std::filesystem::create_directories(channels);
            std::ofstream(channels / "blocked") << "not a directory";
            ChannelMsgLogger::logChannelInfo("blocked", "channel marker");
            require(spdlog::default_logger() == logger,
                    "channel file failure removed the process logger");
            require(spdlog::get(name) == logger,
                    "channel file failure removed the process logger registration");
        } else if (scenario == "channel_concurrent") {
            std::array<std::shared_ptr<spdlog::logger>, 16> channels;
            std::array<std::thread, 16> workers;
            for (std::size_t i = 0; i < workers.size(); ++i) {
                workers[i] = std::thread([&, i] {
                    channels[i] = ChannelMsgLogger::addChannelLogger("shared", LV::info);
                });
            }
            for (auto& worker : workers) worker.join();
            for (const auto& channel : channels) {
                require(channel != nullptr && channel == channels.front(),
                        "concurrent channel creation did not reuse the registered logger");
            }
            require(spdlog::default_logger() == logger,
                    "concurrent channel creation removed the process logger");
        } else if (scenario == "channel_unicode") {
            ChannelMsgLogger::logChannelInfo("BMS-01 \xE6\x80\xBB\xE6\x8E\xA7", "channel marker {}",
                                             104);
            require(spdlog::default_logger() == logger,
                    "Unicode channel name removed the process logger");
        } else if (scenario == "format_failure") {
            DefaultLogger::info("{");
            require(spdlog::default_logger() == logger, "format error removed the process logger");
            spdlog::set_default_logger(nullptr);
            DefaultLogger::trace("missing default logger");
            DefaultLogger::debug("missing default logger");
            DefaultLogger::info("missing default logger");
            DefaultLogger::warn("missing default logger");
            DefaultLogger::error("missing default logger");
            DefaultLogger::critical("missing default logger");
            spdlog::set_default_logger(logger);
        }
        if (scenario == "rotation") {
            const std::string payload(600 * 1024, 'x');
            for (int i = 0; i < 6; ++i) logger->info("{}", payload);
        }
        LOG_TRACE("trace marker");
        LOG_DEBUG("debug marker");
        LOG_INFO("info marker");
        LOG_ERROR("error marker");
    }

    const auto directory = root / "log" / name;
    if (scenario == "channel_unicode") {
        const auto channel_path = root / "log/net/channels/BMS-01 %E6%80%BB%E6%8E%A7" /
#ifdef DEBUG
                                  "BMS-01 %E6%80%BB%E6%8E%A7-debug.log";
#else
                                  "BMS-01 %E6%80%BB%E6%8E%A7.log";
#endif
        std::ifstream channel_file(channel_path);
        const std::string channel_contents{std::istreambuf_iterator<char>(channel_file),
                                           std::istreambuf_iterator<char>()};
        require(channel_contents.find("channel marker 104") != std::string::npos,
                "Unicode channel file or formatted message was not written");
    }
    const auto path = directory / (name + ".log");
    require(std::filesystem::exists(path), "independent log file was not created");
    std::ifstream file(path);
    const std::string contents{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    require((contents.find("error marker") != std::string::npos) == (scenario != "off"),
            "error logging or shutdown flush is incorrect");
    require((contents.find("trace marker") != std::string::npos) == (scenario == "trace"),
            "trace filtering is incorrect");
    require((contents.find("debug marker") != std::string::npos) == (scenario == "trace"),
            "debug filtering is incorrect");
    if (scenario == "rotation") {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            require(entry.file_size() <= 1024 * 1024, "configured rotation size was exceeded");
            ++count;
        }
        require(count == 3, "configured rotation count is incorrect");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::current_path() /
                      ("ems-log-test-" + std::string(argv[1]) + "-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "conf");
#ifdef _WIN32
    _putenv_s("EMS_ROOT", root.string().c_str());
#else
    setenv("EMS_ROOT", root.string().c_str(), 1);
#endif
    try {
        verify(root, argv[1]);
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "; logs: " << root << '\n';
        return 1;
    }
}
