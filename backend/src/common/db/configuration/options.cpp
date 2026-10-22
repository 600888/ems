#include <common/database/mysql_database.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ems::database {
core::Result<MySqlConnectionOptions> load_mysql_options(const std::string& path) {
    try {
        MySqlConnectionOptions options;
        auto file = path;
        if (file.empty()) {
            if (const auto* configured = std::getenv("EMS_DATABASE_CONFIG")) file = configured;
        }
        nlohmann::json json = nlohmann::json::object();
        if (!file.empty()) {
            std::ifstream input(std::filesystem::u8path(file));
            if (!input || !(input >> json) || !json.is_object())
                throw std::invalid_argument("config");
        }
        for (const auto& [key, env] : {std::pair{"host", "EMS_MYSQL_HOST"},
                                       {"user", "EMS_MYSQL_USER"},
                                       {"password", "EMS_MYSQL_PASSWORD"},
                                       {"database", "EMS_MYSQL_DATABASE"}}) {
            if (const auto* value = std::getenv(env)) json[key] = value;
        }
        for (const auto& [key, env] : {std::pair{"port", "EMS_MYSQL_PORT"},
                                       {"max_connections", "EMS_MYSQL_MAX_CONNECTIONS"},
                                       {"acquire_timeout_ms", "EMS_MYSQL_ACQUIRE_TIMEOUT_MS"},
                                       {"io_timeout_seconds", "EMS_MYSQL_IO_TIMEOUT_SECONDS"}}) {
            if (const auto* value = std::getenv(env)) {
                const std::string text(value);
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                    throw std::invalid_argument("number");
                json[key] = std::stoll(text);
            }
        }
        auto positive = [&](const char* key, std::int64_t fallback, std::int64_t max) {
            if (json.contains(key) && !json[key].is_number_integer())
                throw std::invalid_argument("integer");
            const auto value = json.value(key, fallback);
            if (value <= 0 || value > max) throw std::invalid_argument("range");
            return value;
        };
        options.host = json.value("host", options.host);
        options.user = json.value("user", std::string{});
        options.password = json.value("password", std::string{});
        options.database = json.value("database", std::string{});
        options.port = static_cast<std::uint16_t>(positive("port", options.port, 65535));
        options.max_connections = static_cast<std::size_t>(positive("max_connections", 4, 128));
        options.acquire_timeout =
            std::chrono::milliseconds(positive("acquire_timeout_ms", 1000, 60000));
        options.io_timeout_seconds =
            static_cast<unsigned int>(positive("io_timeout_seconds", 5, 60));
        if (options.host.empty() || options.user.empty() || options.database.empty())
            throw std::invalid_argument("identity");
        return options;
    } catch (...) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "invalid MySQL configuration file or environment");
    }
}
}  // namespace ems::database
