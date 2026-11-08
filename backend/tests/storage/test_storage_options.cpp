#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "storage_options.h"

namespace ems::storage {
namespace {
using Json = nlohmann::json;

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
}  // namespace
}  // namespace ems::storage
