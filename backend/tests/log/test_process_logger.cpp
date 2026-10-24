#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify(const std::filesystem::path& root, const std::string& scenario) {
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
