#include <common/database/mysql_database.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "log/default_logger.hpp"
#include "log/process_logger.hpp"
#include "net/collect/collect_engine.h"
#include "net/para_loader/para_loader.h"

namespace {
namespace fs = std::filesystem;
using ems::net::collect::EngineConfig;

volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }

struct Options {
    EngineConfig engine;
    ems::database::MySqlConnectionOptions database;
};

fs::path resolve(const fs::path& base, const std::string& value) {
    const auto path = fs::u8path(value);
    return fs::absolute(path.is_absolute() ? path : base / path).lexically_normal();
}

std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

fs::path executable_directory([[maybe_unused]] const char* executable) {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) throw std::runtime_error("executable path");
    path.resize(length);
    return fs::path(path).parent_path();
#elif defined(__linux__)
    return fs::read_symlink("/proc/self/exe").parent_path();
#else
    return fs::absolute(fs::u8path(executable)).parent_path();
#endif
}

Options load_options(const std::string& file, const char* executable) {
    const auto config_path = fs::absolute(fs::u8path(file));
    std::ifstream input(config_path);
    nlohmann::json json;
    if (!input || !(input >> json) || !json.is_object())
        throw std::invalid_argument("collector configuration");
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.key() != "database_config" && it.key() != "rtdb_config" &&
            it.key() != "scope_code" && it.key() != "plugin_dir" &&
            it.key() != "poll_interval_ms" && it.key() != "max_channels")
            throw std::invalid_argument("unknown collector option");
    }

    Options options;
    const auto base = config_path.parent_path();
    const auto database_file = json.value("database_config", std::string{});
    auto database = ems::database::load_mysql_options(
        database_file.empty() ? std::string{} : utf8(resolve(base, database_file)));
    if (!ems::core::is_ok(database)) throw std::invalid_argument("database configuration");
    options.database = ems::core::value_of(database);
    options.database.require_configuration_schema = true;

    auto& engine = options.engine;
    const auto rtdb_file = json.at("rtdb_config").get<std::string>();
    if (rtdb_file.empty()) throw std::invalid_argument("RTDB configuration");
    engine.rtdbConfigPath_ = utf8(resolve(base, rtdb_file));
    engine.rtdbScopeCode_ = json.at("scope_code").get<std::string>();
    if (engine.rtdbScopeCode_.empty()) throw std::invalid_argument("scope code");

    const auto plugin_dir = json.value("plugin_dir", std::string{});
    engine.pluginDir_ = utf8((plugin_dir.empty() ? resolve(executable_directory(executable),
                                                           EMS_COLLECTOR_PLUGIN_RELATIVE_DIR)
                                                 : resolve(base, plugin_dir)));
    if (!fs::is_directory(fs::u8path(engine.pluginDir_)))
        throw std::invalid_argument("plugin directory");
    if (!fs::is_regular_file(fs::u8path(engine.rtdbConfigPath_)))
        throw std::invalid_argument("RTDB configuration file");

    auto positive = [&](const char* key, int fallback, int maximum) {
        if (json.contains(key) && !json[key].is_number_integer())
            throw std::invalid_argument("integer option");
        const auto value = json.value(key, std::int64_t{fallback});
        if (value <= 0 || value > maximum) throw std::invalid_argument("option range");
        return static_cast<int>(value);
    };
    engine.defaultPollIntervalMs_ = positive("poll_interval_ms", 1000, 3600000);
    engine.maxChannels_ = positive("max_channels", 256, 65536);
    // 正式入口只写统一实时库，可选的旁路通知不作为启动依赖。
    engine.notificationBackend_ = ems::net::collect::NotificationBackend::None;
    return options;
}
}  // namespace

int main(int argc, char** argv) {
    const ProcessLogger logger("ems-collector");
    std::string config = "conf/collector.json";
    bool check = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "ems-collector [--config path] [--check-config]\n";
            return 0;
        }
        if (argument == "--check-config") {
            check = true;
        } else if (argument == "--config" && i + 1 < argc) {
            config = argv[++i];
        } else {
            std::cerr << "ems-collector: invalid arguments; use --help\n";
            return 2;
        }
    }

    try {
        const auto options = load_options(config, argv[0]);
        if (check) {
            std::cout << "Collector configuration valid (database and RTDB not connected)\n";
            return 0;
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
#if defined(_WIN32)
        std::signal(SIGBREAK, stop);
#endif

        auto database = ems::database::connect_mysql_database(
            options.database, ems::core::make_call_context(std::chrono::seconds(10)));
        if (!ems::core::is_ok(database)) {
            LOG_ERROR("ems-collector database unavailable: {}",
                      ems::core::to_string(ems::core::error_of(database).code));
            std::cerr << "ems-collector: database unavailable; check configuration and schema\n";
            return 1;
        }
        ems::net::device::ParaLoader loader;
        if (!loader.initialize(ems::core::value_of(database),
                               ems::core::make_call_context(std::chrono::seconds(10))))
            throw std::runtime_error("configuration load failed");
        const auto channels = loader.load_channels();
        if (channels.size() > static_cast<std::size_t>(options.engine.maxChannels_))
            throw std::runtime_error("channel limit exceeded");

        ems::net::collect::CollectEngine engine;
        if (!engine.initialize(options.engine)) throw std::runtime_error("engine initialization");
        if (stopping) return 0;
        const auto started = engine.start_all(loader);
        if (started < 0 || static_cast<std::size_t>(started) != channels.size())
            throw std::runtime_error("channel startup failed");
        LOG_INFO("ems-collector started, channels={}", started);
        std::cout << "ems-collector started, channels=" << started << std::endl;
        while (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        engine.shutdown();
        LOG_INFO("ems-collector stopped");
        return 0;
    } catch (const std::exception&) {
        LOG_ERROR("ems-collector: check configuration, database, RTDB and protocol plugins");
        std::cerr << "ems-collector: check configuration, database, RTDB and protocol plugins\n";
        return 1;
    }
}
