#include "storage_options.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace ems::storage {
core::Result<StorageOptions> load_storage_options(const std::string& path) {
    try {
        const auto file = std::filesystem::absolute(std::filesystem::u8path(path));
        if (std::filesystem::file_size(file) > 1024 * 1024) throw std::invalid_argument("size");
        std::ifstream input(file);
        nlohmann::json json;
        if (!input || !(input >> json) || !json.is_object()) throw std::invalid_argument("config");
        const std::set<std::string> allowed{"database_config", "rtdb_config", "timeseries_config",
                                            "scope_code", "interval_ms"};
        for (auto it = json.begin(); it != json.end(); ++it)
            if (!allowed.count(it.key())) throw std::invalid_argument("unknown option");
        const auto resolve = [&](const char* key) {
            const auto text = json.at(key).get<std::string>();
            if (text.empty()) throw std::invalid_argument("empty path");
            auto p = std::filesystem::u8path(text);
            return (p.is_absolute() ? p : file.parent_path() / p).lexically_normal().u8string();
        };
        StorageOptions options;
        auto mysql = database::load_mysql_options(resolve("database_config"));
        if (!core::is_ok(mysql)) return core::error_of(mysql);
        options.database = core::value_of(mysql);
        options.database.require_configuration_schema = true;
        auto series = database::load_time_series_options(resolve("timeseries_config"));
        if (!core::is_ok(series)) return core::error_of(series);
        options.timeseries = core::value_of(series);
        options.realtime_config = resolve("rtdb_config");
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(options.realtime_config)))
            throw std::invalid_argument("RTDB configuration missing");
        options.scope_code = json.at("scope_code").get<std::string>();
        if (json.contains("interval_ms") && !json.at("interval_ms").is_number_integer())
            throw std::invalid_argument("integer interval required");
        options.interval_ms = json.value("interval_ms", kDefaultArchiveIntervalMs);
        if (options.scope_code.empty() || options.interval_ms < 1000 ||
            options.interval_ms > 3600000)
            throw std::invalid_argument("archive option bounds");
        return options;
    } catch (const std::exception&) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "invalid storage configuration");
    }
}
}  // namespace ems::storage
