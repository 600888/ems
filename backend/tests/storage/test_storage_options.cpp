#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "storage_options.h"

namespace ems::storage {
namespace {
using Json = nlohmann::json;

class EnvironmentValue {
   public:
    EnvironmentValue(const char* name, const char* value) : name_(name) {
        if (const auto* previous = std::getenv(name)) previous_ = previous;
        set(value);
    }

    ~EnvironmentValue() { set(previous_ ? previous_->c_str() : nullptr); }

   private:
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name_, value ? value : "");
#else
        if (value)
            setenv(name_, value, 1);
        else
            unsetenv(name_);
#endif
    }

    const char* name_;
    std::optional<std::string> previous_;
};

class StorageOptionsTest : public ::testing::Test {
   protected:
    std::filesystem::path directory;

    void write(const char* name, const Json& body) {
        std::ofstream(directory / name) << body.dump();
    }

    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
                    ("ems-storage-options-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        write("mysql.json", {{"host", "127.0.0.1"}, {"user", "test"}, {"database", "ems"}});
        write("series.json", {{"user", "test"}, {"database", "ems_history"}});
        write("realtime.json", Json::object());
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    Json config() {
        return {{"database_config", "mysql.json"},
                {"timeseries_config", "series.json"},
                {"rtdb_config", "realtime.json"},
                {"scope_code", "station001"}};
    }

    core::Result<StorageOptions> load(Json json) {
        write("storage.json", json);
        return load_storage_options((directory / "storage.json").u8string());
    }
};

TEST_F(StorageOptionsTest, DefaultsToFiveSecondsAndResolvesPathsRelativeToConfig) {
    auto result = load(config());
    ASSERT_TRUE(core::is_ok(result));
    const auto& options = core::value_of(result);
    EXPECT_EQ(options.interval_ms, 5000);
    EXPECT_EQ(options.realtime_config, (directory / "realtime.json").u8string());
    EXPECT_EQ(options.timeseries.taos.port, 6030);
    EXPECT_TRUE(options.database.require_configuration_schema);
}

TEST_F(StorageOptionsTest, RejectsInvalidIntervalsUnknownOptionsAndMissingDependency) {
    for (const auto& interval :
         {Json(0), Json(-1), Json(999), Json(3600001), Json(1.5), Json("5000")}) {
        auto json = config();
        json["interval_ms"] = interval;
        EXPECT_FALSE(core::is_ok(load(json)));
    }
    auto json = config();
    json["version"] = 1;
    EXPECT_FALSE(core::is_ok(load(json)));
    json = config();
    json["timeseries_config"] = "missing.json";
    EXPECT_FALSE(core::is_ok(load(json)));
}

TEST_F(StorageOptionsTest, RejectsInvalidTimeSeriesPortAndDoesNotIncludeSecretsInErrors) {
    for (const auto& port : {Json(0), Json(65536), Json("6030"), Json(6030.5)}) {
        write("series.json", {{"user", "test"}, {"password", "never-print-this"}, {"port", port}});
        auto result = load(config());
        ASSERT_FALSE(core::is_ok(result));
        EXPECT_EQ(core::error_of(result).message.find("never-print-this"), std::string::npos);
    }
}

TEST_F(StorageOptionsTest, SelectsInfluxDbWithoutTaosCredentialsOrClientDirectory) {
    write("series.json", {{"backend", "influxdb"},
                          {"url", "https://localhost:8086"},
                          {"database", "history_mapping"},
                          {"token", "never-print-this"},
                          {"timeout_ms", 2500}});
    auto result = load(config());
    ASSERT_TRUE(core::is_ok(result));
    const auto& series = core::value_of(result).timeseries;
    EXPECT_EQ(series.backend, database::TimeSeriesBackend::InfluxDb);
    EXPECT_EQ(series.influxdb.url, "https://localhost:8086");
    EXPECT_EQ(series.influxdb.database, "history_mapping");
    EXPECT_EQ(series.influxdb.timeout_ms, 2500u);
}

TEST_F(StorageOptionsTest, RejectsInvalidInfluxDbConfigurationWithoutExposingToken) {
    const Json valid{
        {"backend", "influxdb"}, {"url", "http://localhost:8086"}, {"token", "never-print-this"}};
    for (const auto& [key, value] :
         std::vector<std::pair<std::string, Json>>{{"url", "ftp://localhost"},
                                                   {"url", "http://user:password@localhost"},
                                                   {"url", "http://localhost?db=other"},
                                                   {"url", "http://localhost:0"},
                                                   {"url", "http://localhost:65536"},
                                                   {"url", "http://localhost#fragment"},
                                                   {"token", "bad\r\nheader"},
                                                   {"token", ""},
                                                   {"database", "bad?db=other"},
                                                   {"database", ""},
                                                   {"timeout_ms", 0},
                                                   {"timeout_ms", 300001},
                                                   {"timeout_ms", "10000"},
                                                   {"timeout_ms", 2.5}}) {
        auto json = valid;
        json[key] = value;
        write("series.json", json);
        auto result = load(config());
        ASSERT_FALSE(core::is_ok(result)) << key << "=" << value;
        EXPECT_EQ(core::error_of(result).code, core::ErrorCode::InvalidArgument);
        EXPECT_EQ(core::error_of(result).message.find("never-print-this"), std::string::npos);
    }
}

TEST_F(StorageOptionsTest, BackendSpecificEnvironmentOverridesAreIsolated) {
    const EnvironmentValue taos_database("EMS_TAOS_DATABASE", "wrong-database");
    const EnvironmentValue taos_port("EMS_TAOS_PORT", "bad-port");
    const EnvironmentValue taos_directory("EMS_TAOS_CONFIG_DIR", "missing-directory");
    const EnvironmentValue influx_url("EMS_INFLUXDB_URL", "https://localhost:8087");
    const EnvironmentValue influx_database("EMS_INFLUXDB_DATABASE", "env_mapping");
    const EnvironmentValue influx_token("EMS_INFLUXDB_TOKEN", "env-token");
    const EnvironmentValue backend("EMS_STORAGE_SERIES_BACKEND", "influxdb");
    auto result = load(config());
    ASSERT_TRUE(core::is_ok(result));
    const auto& series = core::value_of(result).timeseries;
    EXPECT_EQ(series.backend, database::TimeSeriesBackend::InfluxDb);
    EXPECT_EQ(series.influxdb.url, "https://localhost:8087");
    EXPECT_EQ(series.influxdb.database, "env_mapping");
    EXPECT_EQ(series.influxdb.token, "env-token");
}
}  // namespace
}  // namespace ems::storage
