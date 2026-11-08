#include <common/codec/digest.h>
#include <common/core/time.h>
#include <gtest/gtest.h>

#include <map>
#include <nlohmann/json.hpp>

#include "sampling.h"
#include "storage_options.h"

namespace ems::storage {
namespace {
using namespace database;
using Json = nlohmann::json;

DeviceConfigRecords records() {
    DeviceConfigRecords result;
    CfgDeviceRow station;
    station.device_id = "station";
    station.node_kind = "station";
    station.config_revision = 7;
    station.default_stale_after_ms = 60000;
    CfgDeviceRow unit;
    unit.device_id = "unit";
    unit.parent_device_id = "station";
    unit.node_kind = "unit";
    CfgDeviceRow device;
    device.device_id = "device";
    device.parent_device_id = "unit";
    device.node_kind = "device";
    result.devices = {station, unit, device};
    CfgPointYcRow yc;
    yc.point_id = "power";
    yc.owner_device_id = "device";
    yc.value_type = "double";
    result.points_yc.push_back(yc);
    CfgPointYxRow yx;
    yx.point_id = "state";
    yx.owner_device_id = "device";
    yx.value_type = "bool";
    result.points_yx.push_back(yx);
    CfgPointYkRow yk;
    yk.point_id = "command";
    yk.owner_device_id = "device";
    yk.value_type = "bool";
    result.points_yk.push_back(yk);
    CfgPointYtRow yt;
    yt.point_id = "setpoint";
    yt.owner_device_id = "device";
    yt.value_type = "double";
    result.points_yt.push_back(yt);
    return result;
}

struct Store final : ITimeSeriesStore {
    std::vector<Sample> samples;
    bool fail{false};

    core::Status write(const Sample& sample, const core::CallContext&) override {
        if (fail) return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown);
        samples.push_back(sample);
        return core::ok_status();
    }
};

core::CallContext context() { return core::make_call_context(std::chrono::seconds(5)); }

Json payload(const std::string& id, Json value, std::int64_t age_ms = 0, int quality = 0) {
    return {{"code", id},
            {"timestamp", core::now_utc_micros() / 1000 - age_ms},
            {"quality", quality},
            {"value", value}};
}

TEST(StorageSampling, DirectoryCoversFourTablesAndUsesStableMappings) {
    auto config = records();
    auto result = build_archive_directory(config);
    ASSERT_TRUE(core::is_ok(result));
    const auto directory = core::value_of(result);
    ASSERT_EQ(directory.points.size(), 4u);
    EXPECT_EQ(directory.config_revision, 7);
    EXPECT_EQ(directory.points[0].scope_kind, "unit");
    EXPECT_EQ(directory.points[0].scope_id, "unit");
    EXPECT_EQ(directory.points[0].series.series_key, "p_" + codec::sha256("power"));
    EXPECT_EQ(directory.points[2].point_type, "yk");
    EXPECT_EQ(directory.points[3].point_type, "yt");
    config.devices[2].name = "renamed";
    config.devices[0].config_revision++;
    EXPECT_EQ(core::value_of(build_archive_directory(config)).points[0].series.series_key,
              directory.points[0].series.series_key);
    EXPECT_EQ(kDefaultArchiveIntervalMs, 5000);
}

TEST(StorageSampling, RejectsCyclesOrphansDuplicateIdentitiesAndUnknownValueTypes) {
    auto config = records();
    config.devices[2].parent_device_id = "device";
    EXPECT_FALSE(core::is_ok(build_archive_directory(config)));
    config = records();
    config.points_yc[0].owner_device_id = "missing";
    EXPECT_FALSE(core::is_ok(build_archive_directory(config)));
    config = records();
    config.points_yx[0].point_id = "power";
    EXPECT_FALSE(core::is_ok(build_archive_directory(config)));
    config = records();
    config.points_yc[0].value_type = "nonsense";
    EXPECT_FALSE(core::is_ok(build_archive_directory(config)));
}

TEST(StorageSampling, RepeatsUnchangedValuesAtFiveSecondSlotsAndRetainsMeasurementTime) {
    const auto directory = core::value_of(build_archive_directory(records()));
    Store store;
    std::map<std::string, Json> live;
    for (const auto& point : directory.points)
        live[point.series.point_id] = payload(
            point.series.point_id,
            point.series.value_type == ValueType::Boolean ? Json(true) : Json::parse("12.5"));
    PeriodicArchiveSampler sampler(
        [&](const auto&, const auto& id, const auto&) -> core::Result<std::string> {
            return live.at(id).dump();
        },
        store);
    const auto slot = core::now_utc_micros();
    EXPECT_EQ(sampler.sample(directory, slot, context()).written, 4u);
    EXPECT_EQ(sampler.sample(directory, slot + 5000000, context()).written, 4u);
    ASSERT_EQ(store.samples.size(), 8u);
    EXPECT_EQ(store.samples[4].source_time_us - store.samples[0].source_time_us, 5000000);
    EXPECT_EQ(store.samples[4].measurement_time_us, store.samples[0].measurement_time_us);
    EXPECT_NE(store.samples[4].event_id, store.samples[0].event_id);
    EXPECT_EQ(store.samples[0].config_revision, "7");
    EXPECT_EQ(Json(std::get<double>(store.samples[0].value)).dump(), "12.5");
    EXPECT_TRUE(sampler.sample(directory, slot + 5000000, context()).duplicate_slot);
    EXPECT_EQ(store.samples.size(), 8u);
}

TEST(StorageSampling, ArchivesMissingDisabledStaleAndInvalidAsNullWithQuality) {
    auto directory = core::value_of(build_archive_directory(records()));
    directory.points[1].enabled = false;
    Store store;
    int reads = 0;
    PeriodicArchiveSampler sampler(
        [&](const auto&, const auto& id, const auto&) -> core::Result<std::string> {
            ++reads;
            if (id == "power")
                return core::make_error(core::ErrorCode::NotFound,
                                        core::WriteEffect::NotApplicable);
            if (id == "command") return payload(id, true, 70000).dump();
            return payload(id, "corrupt").dump();
        },
        store);
    const auto result = sampler.sample(directory, core::now_utc_micros(), context());
    EXPECT_EQ(result.written, 4u);
    EXPECT_EQ(result.invalid, 4u);
    EXPECT_EQ(reads, 3);
    ASSERT_EQ(store.samples.size(), 4u);
    EXPECT_EQ(store.samples[0].quality_reason, "sample_missing");
    EXPECT_EQ(store.samples[1].quality, 10);
    EXPECT_EQ(store.samples[2].quality, 8);
    EXPECT_EQ(store.samples[3].quality, 2);
    for (const auto& sample : store.samples)
        EXPECT_TRUE(std::holds_alternative<std::monostate>(sample.value));
}

TEST(StorageSampling, PreservesSignedIntegerAndUnsignedTextWithoutDoubleConversion) {
    auto config = records();
    config.points_yc[0].value_type = "int64";
    config.points_yt[0].value_type = "uint64";
    const auto directory = core::value_of(build_archive_directory(config));
    Store store;
    PeriodicArchiveSampler sampler(
        [&](const auto&, const auto& id, const auto&) -> core::Result<std::string> {
            if (id == "power") return payload(id, Json(std::int64_t{9007199254740993LL})).dump();
            if (id == "setpoint") return payload(id, "18446744073709551615").dump();
            auto item = payload(id, Json::parse("1.0"));
            return item.dump();
        },
        store);
    EXPECT_EQ(sampler.sample(directory, core::now_utc_micros(), context()).invalid, 0u);
    for (const auto& sample : store.samples)
        EXPECT_EQ(sample.quality, 0) << sample.point_id << ": " << sample.quality_reason;
    ASSERT_TRUE(std::holds_alternative<std::int64_t>(store.samples[0].value));
    EXPECT_EQ(std::get<std::int64_t>(store.samples[0].value), 9007199254740993LL);
    EXPECT_EQ(std::get<std::string>(store.samples[3].value), "18446744073709551615");
    EXPECT_TRUE(std::get<bool>(store.samples[1].value));
}

TEST(StorageSampling, CountsWriteFailuresAndStopsWhenBudgetExpires) {
    const auto directory = core::value_of(build_archive_directory(records()));
    Store store;
    store.fail = true;
    PeriodicArchiveSampler sampler(
        [](const auto&, const auto& id, const auto&) -> core::Result<std::string> {
            return payload(id, 1).dump();
        },
        store);
    auto now = core::now_utc_micros();
    auto result = sampler.sample(directory, now, context());
    EXPECT_EQ(result.written, 0u);
    EXPECT_EQ(result.failed, 4u);
    store.fail = false;
    result = sampler.sample(directory, now + 5000000, core::immediate_context());
    EXPECT_EQ(result.failed, 4u);
    EXPECT_TRUE(store.samples.empty());
}

TEST(StorageSampling, UnsignedSamplesAcceptExactValuesAndRejectOverflowOrInexactFloats) {
    auto config = records();
    config.points_yc[0].value_type = "uint64";
    config.points_yx.clear();
    config.points_yk.clear();
    config.points_yt.clear();
    const auto directory = core::value_of(build_archive_directory(config));
    const std::vector<std::pair<Json, bool>> cases = {{Json::parse("42.0"), false},
                                                      {Json("18446744073709551615"), false},
                                                      {Json("18446744073709551616"), true},
                                                      {Json("-1"), true},
                                                      {Json::parse("9007199254740992.0"), true}};
    for (const auto& [value, invalid] : cases) {
        Store store;
        PeriodicArchiveSampler sampler(
            [&](const auto&, const auto& id, const auto&) -> core::Result<std::string> {
                return payload(id, value).dump();
            },
            store);
        const auto report = sampler.sample(directory, core::now_utc_micros(), context());
        EXPECT_EQ(report.written, 1u);
        EXPECT_EQ(report.invalid, invalid ? 1u : 0u);
    }
}

TEST(StorageSampling, Sha256UsesKnownVectors) {
    EXPECT_EQ(codec::sha256(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(codec::sha256("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
}  // namespace
}  // namespace ems::storage
