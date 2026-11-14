#include "influxdb/options.h"

#include <common/database/timeseries/factory.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace ems::database {
core::Result<TimeSeriesConnectionOptions> load_time_series_options(const std::string& path) {
    try {
        auto file = path;
        if (file.empty()) {
            if (const auto* value = std::getenv("EMS_TIMESERIES_CONFIG")) file = value;
        }
        auto json = nlohmann::json::object();
        if (!file.empty()) {
            const auto p = std::filesystem::u8path(file);
            if (std::filesystem::file_size(p) > 1024 * 1024) throw std::invalid_argument("size");
            std::ifstream input(p);
            if (!input || !(input >> json) || !json.is_object())
                throw std::invalid_argument("configuration");
        }
        const std::set<std::string> allowed{
            "backend",           "host", "port",  "user",      "password", "database",
            "client_config_dir", "url",  "token", "timeout_ms"};
        for (auto it = json.begin(); it != json.end(); ++it)
            if (!allowed.count(it.key())) throw std::invalid_argument("unknown option");
        if (const auto* value = std::getenv("EMS_STORAGE_SERIES_BACKEND")) json["backend"] = value;
        TimeSeriesConnectionOptions result;
        auto backend = parse_time_series_backend(json.value("backend", "taos"));
        if (!core::is_ok(backend)) return core::error_of(backend);
        result.backend = core::value_of(backend);
        if (result.backend == TimeSeriesBackend::Tdengine) {
            for (const auto& [key, env] : {std::pair{"host", "EMS_TAOS_HOST"},
                                           {"user", "EMS_TAOS_USER"},
                                           {"password", "EMS_TAOS_PASSWORD"},
                                           {"database", "EMS_TAOS_DATABASE"},
                                           {"client_config_dir", "EMS_TAOS_CONFIG_DIR"}})
                if (const auto* value = std::getenv(env)) json[key] = value;
            if (const auto* value = std::getenv("EMS_TAOS_PORT")) {
                const std::string text(value);
                if (text.empty() || text.size() > 5 ||
                    text.find_first_not_of("0123456789") != std::string::npos)
                    throw std::invalid_argument("port");
                json["port"] = std::stoi(text);
            }
        }
        if (result.backend == TimeSeriesBackend::InfluxDb) {
            for (const auto& [key, env] : {std::pair{"url", "EMS_INFLUXDB_URL"},
                                           {"database", "EMS_INFLUXDB_DATABASE"},
                                           {"token", "EMS_INFLUXDB_TOKEN"}})
                if (const auto* value = std::getenv(env)) json[key] = value;
            auto& influx = result.influxdb;
            influx.url = json.value("url", influx.url);
            influx.database = json.value("database", influx.database);
            influx.token = json.value("token", std::string{});
            if (json.contains("timeout_ms") && !json.at("timeout_ms").is_number_integer())
                throw std::invalid_argument("timeout type");
            const auto timeout = json.value("timeout_ms", std::int64_t{10000});
            if (timeout < 1 || timeout > 300000) throw std::invalid_argument("timeout bounds");
            influx.timeout_ms = static_cast<std::uint32_t>(timeout);
            if (!influx::valid_options(influx)) throw std::invalid_argument("influxdb options");
            return result;
        }
        auto& taos = result.taos;
        taos.host = json.value("host", taos.host);
        taos.user = json.value("user", std::string{});
        taos.password = json.value("password", std::string{});
        taos.database = json.value("database", taos.database);
        const auto config_dir = json.value("client_config_dir", std::string{});
        if (!config_dir.empty()) {
            auto directory = std::filesystem::u8path(config_dir);
            if (directory.is_relative() && !file.empty())
                directory = std::filesystem::u8path(file).parent_path() / directory;
            directory = std::filesystem::absolute(directory).lexically_normal();
            if (!std::filesystem::is_directory(directory))
                throw std::invalid_argument("client configuration directory");
            taos.config_dir = directory.u8string();
        }
        if (json.contains("port") && !json.at("port").is_number_integer())
            throw std::invalid_argument("port type");
        const auto port = json.value("port", std::int64_t{6030});
        if (port < 1 || port > 65535 || taos.host.empty() || taos.user.empty() ||
            taos.database != "ems_history")
            throw std::invalid_argument("connection bounds");
        taos.port = static_cast<std::uint16_t>(port);
        return result;
    } catch (const std::exception&) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "invalid time-series configuration");
    }
}
}  // namespace ems::database
