/**
 * @file test_realtime_service.cpp
 * @brief 类型化实时门面测试：运行目录、测点序号/来源、新鲜度、能力门控与通道统计。
 */
#include <gtest/gtest.h>
#include <rtdb/client/realtime_service.h>
#include <rtdb/model/runtime_catalog.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ems::rtdb {
namespace {

core::CallContext ctx() { return core::make_call_context(std::chrono::milliseconds(500)); }

/// 内存后端：只实现门面依赖的单记录语义，CAS 冲突行为与真实后端一致。
class MemoryStore final : public RealtimeStoreBase {
   public:
    BackendInfo info() const override {
        BackendInfo info;
        info.implementation = "memory";
        info.max_record_bytes = 16384;
        info.max_batch_items = 256;
        return info;
    }

    Result<Record> read(const RecordKey& key, const CallContext&) override {
        const auto it = records_.find(keyOf(key));
        if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplicable, "no record");
        }
        return it->second;
    }

    Result<WriteReceipt> compareExchange(const WriteRequest& request, const CallContext&) override {
        const std::string key = keyOf(request.key);
        const auto it = records_.find(key);
        if (conflict_next) {
            conflict_next = false;
            return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "injected");
        }
        if (!request.expected.has_value()) {
            if (it != records_.end()) {
                return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "exists");
            }
        } else if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplied, "missing");
        } else if (!(it->second.revision == *request.expected)) {
            return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "cas mismatch");
        }
        WriteReceipt receipt;
        receipt.revision.opaque = "r" + std::to_string(++counter_);
        records_[key] = Record{request.replacement, receipt.revision};
        return receipt;
    }

    Result<EraseReceipt> erase(const EraseRequest& request, const CallContext&) override {
        const auto it = records_.find(keyOf(request.key));
        if (it == records_.end()) {
            return core::make_error(ErrorCode::NotFound, WriteEffect::NotApplied, "missing");
        }
        if (!(it->second.revision == request.expected)) {
            return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied, "cas mismatch");
        }
        records_.erase(it);
        return EraseReceipt{};
    }

    void close() noexcept override {}

    bool conflict_next{false};

   private:
    static std::string keyOf(const RecordKey& key) {
        std::string text = key.scope.kind == ScopeKind::Station ? "s:" : "u:";
        text += key.scope.code + "|" + std::to_string(static_cast<int>(key.kind)) + "|";
        for (const auto& part : key.device_path) text += part + "/";
        text += "|" + key.name;
        return text;
    }

    std::map<std::string, Record> records_;
    std::uint64_t counter_{0};
};

core::AssetId assetId(const char* value) { return core::AssetId{value}; }

model::ModelCatalog makeCatalog(bool power_enabled = true) {
    model::ModelCatalog catalog;

    model::AssetNode station;
    station.asset_id = assetId("st1");
    station.station_id = core::StationId{"st1"};
    station.code = "st1";
    station.name = "测试站";
    station.asset_type = model::AssetType::Station;
    catalog.assets.push_back(station);

    model::AssetNode unit;
    unit.asset_id = assetId("unit01");
    unit.station_id = core::StationId{"st1"};
    unit.parent_asset_id = assetId("st1");
    unit.code = "unit01";
    unit.name = "储能单元 01";
    unit.asset_type = model::AssetType::EssUnit;
    catalog.assets.push_back(unit);

    model::AssetNode pcs_asset;
    pcs_asset.asset_id = assetId("pcs_asset");
    pcs_asset.station_id = core::StationId{"st1"};
    pcs_asset.parent_asset_id = assetId("unit01");
    pcs_asset.code = "pcs01";
    pcs_asset.name = "储能变流器 01";
    pcs_asset.asset_type = model::AssetType::Device;
    catalog.assets.push_back(pcs_asset);

    model::AssetNode rack;
    rack.asset_id = assetId("rack01");
    rack.station_id = core::StationId{"st1"};
    rack.parent_asset_id = assetId("unit01");
    rack.code = "rack01";
    rack.name = "电池簇 01";
    rack.asset_type = model::AssetType::Part;
    catalog.assets.push_back(rack);

    model::Device device;
    device.device_id = core::DeviceId{"dev1"};
    device.asset_id = assetId("pcs_asset");
    device.code = "pcs01";
    device.name = "储能变流器 01";
    device.type_version_id = "pcs-v1";
    device.model_version_id = "pcs-125k-v23";
    device.vendor = "示例厂商";
    device.firmware_version = "FW2.1.6";
    device.enabled = true;
    catalog.devices.push_back(device);

    model::YcPoint power;
    power.point_id = core::PointId{"p_power"};
    power.asset_id = assetId("pcs_asset");
    power.code = "active_power";
    power.name = "有功功率";
    power.value_type = model::ValueType::Double;
    power.unit = "kW";
    power.enabled = power_enabled;
    catalog.add_point(power);

    model::YxPoint status;
    status.point_id = core::PointId{"p_run"};
    status.asset_id = assetId("pcs_asset");
    status.code = "run_state";
    status.name = "运行状态";
    status.value_type = model::ValueType::String;
    status.enabled = true;
    catalog.add_point(status);

    model::YcPoint soc;
    soc.point_id = core::PointId{"p_soc"};
    soc.asset_id = assetId("rack01");
    soc.code = "soc";
    soc.name = "SOC";
    soc.value_type = model::ValueType::Double;
    soc.unit = "%";
    soc.enabled = true;
    catalog.add_point(soc);

    model::ChannelConfig channel;
    channel.channel_id = core::ChannelId{"ch01"};
    channel.station_id = core::StationId{"st1"};
    channel.code = "ch01";
    channel.name = "主采集";
    channel.enabled = true;
    catalog.channels.push_back(channel);

    return catalog;
}

RuntimeCatalogOptions makeOptions() {
    RuntimeCatalogOptions options;
    options.scope = Scope{ScopeKind::Unit, "unit01"};
    options.config_version = "v13";
    options.activation_revision = "6";
    options.default_stale_after_ms = 60000;
    options.default_source.source_id = "src1";
    options.default_source.epoch = core::SourceEpoch{7};
    options.default_source.config_version = "v13";
    options.default_source.write_enabled = true;
    return options;
}

PointAddress powerAddress() {
    return PointAddress{Scope{ScopeKind::Unit, "unit01"}, {"pcs01"}, "active_power"};
}

PointAddress runAddress() {
    return PointAddress{Scope{ScopeKind::Unit, "unit01"}, {"pcs01"}, "run_state"};
}

PointAddress socAddress() {
    return PointAddress{Scope{ScopeKind::Unit, "unit01"}, {"rack01"}, "soc"};
}

model::PointSample sample(double value, std::uint64_t sequence, core::UtcMicros time) {
    model::PointSample result;
    result.value = model::PointValue{value};
    result.quality.code = model::Quality::Good;
    result.sequence = sequence;
    result.source_epoch = core::SourceEpoch{7};
    result.source_id = "src1";
    result.config_version = "v13";
    result.source_time = time;
    result.ingest_time = time;
    return result;
}

class RealtimeServiceTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto built = buildRuntimeCatalog(makeCatalog(), makeOptions());
        ASSERT_TRUE(built.ok()) << (built.issues.empty() ? "" : built.issues.front());
        built.catalog.addCapability(
            RuntimeCapability{core::DeviceId{"dev1"},
                              "monitor_power",
                              {core::PointId{"p_power"}, core::PointId{"p_run"}},
                              {}});
        catalog_ = std::make_shared<const RuntimeCatalog>(built.catalog);
        store_ = std::make_shared<MemoryStore>();
        now_ = 1'700'000'000'000'000;
        service_ = std::make_unique<RealtimeService>(store_, catalog_, [this] { return now_; });
    }

    RealtimeService& service() { return *service_; }

    MemoryStore& store() { return *store_; }

    std::shared_ptr<MemoryStore> store_;
    std::shared_ptr<const RuntimeCatalog> catalog_;
    std::unique_ptr<RealtimeService> service_;
    core::UtcMicros now_{0};
};

TEST_F(RealtimeServiceTest, CatalogResolvesAssetPathsWithoutInventingDevices) {
    EXPECT_EQ(3u, catalog_->pointCount());
    EXPECT_EQ(1u, catalog_->deviceCount());
    EXPECT_EQ(1u, catalog_->channelCount());
    ASSERT_NE(nullptr, catalog_->findPoint(powerAddress()));
    ASSERT_NE(nullptr, catalog_->findPoint(socAddress()));
    const RuntimePoint* soc = catalog_->findPoint(socAddress());
    ASSERT_NE(nullptr, soc);
    EXPECT_TRUE(soc->device_id.empty());  // 部件点不虚构通信设备
    EXPECT_EQ(std::vector<std::string>({"rack01"}), soc->address.device_path);
    ASSERT_NE(nullptr, catalog_->findChannel(core::ChannelId{"ch01"}));
    EXPECT_EQ(ScopeKind::Station, catalog_->findChannel(core::ChannelId{"ch01"})->scope.kind);
}

TEST_F(RealtimeServiceTest, AppliesAndReadsTypedPointSample) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    auto applied = service().updatePoint(powerAddress(), sample(82.6, 1, now_ - 1000000), ctx());
    ASSERT_TRUE(core::is_ok(applied));
    EXPECT_EQ(SampleResult::Applied, core::value_of(applied));

    auto read = service().getPoint(powerAddress(), ctx());
    ASSERT_TRUE(core::is_ok(read));
    const auto& snapshot = core::value_of(read);
    ASSERT_TRUE(snapshot.value.has_value());
    EXPECT_DOUBLE_EQ(82.6, std::get<double>(*snapshot.value));
    EXPECT_EQ(model::Quality::Good, snapshot.quality.code);
    EXPECT_FALSE(snapshot.stale);
    EXPECT_TRUE(snapshot.config_current);
    EXPECT_TRUE(snapshot.source_authorized);
    EXPECT_TRUE(snapshot.writable);
    EXPECT_EQ(1u, snapshot.sequence);
}

TEST_F(RealtimeServiceTest, RejectsOutOfOrderDuplicateAndEqualSequenceConflict) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    ASSERT_TRUE(core::is_ok(service().updatePoint(powerAddress(), sample(10.0, 2, now_), ctx())));

    auto older = service().updatePoint(powerAddress(), sample(9.0, 1, now_), ctx());
    ASSERT_TRUE(core::is_ok(older));
    EXPECT_EQ(SampleResult::IgnoredOutOfOrder, core::value_of(older));

    auto duplicate = service().updatePoint(powerAddress(), sample(10.0, 2, now_), ctx());
    ASSERT_TRUE(core::is_ok(duplicate));
    EXPECT_EQ(SampleResult::Duplicate, core::value_of(duplicate));

    auto conflict = service().updatePoint(powerAddress(), sample(11.0, 2, now_), ctx());
    ASSERT_FALSE(core::is_ok(conflict));
    EXPECT_EQ(core::ErrorCode::Conflict, core::error_of(conflict).code);

    auto read = service().getPoint(powerAddress(), ctx());
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_DOUBLE_EQ(10.0, std::get<double>(*core::value_of(read).value));
}

TEST_F(RealtimeServiceTest, RejectsUnauthorizedSourceAndStaleConfigVersion) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    auto sample_value = sample(1.0, 1, now_);
    sample_value.source_id = "intruder";
    auto denied = service().updatePoint(powerAddress(), sample_value, ctx());
    ASSERT_FALSE(core::is_ok(denied));
    EXPECT_EQ(core::ErrorCode::PermissionDenied, core::error_of(denied).code);

    auto outdated = sample(1.0, 1, now_);
    outdated.config_version = "v12";
    auto rejected = service().updatePoint(powerAddress(), outdated, ctx());
    ASSERT_FALSE(core::is_ok(rejected));
    EXPECT_EQ(core::ErrorCode::PermissionDenied, core::error_of(rejected).code);
}

TEST_F(RealtimeServiceTest, AuthorizePointSwitchesSourceAndRejectsOldSamples) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    ASSERT_TRUE(core::is_ok(service().updatePoint(powerAddress(), sample(10.0, 5, now_), ctx())));

    SourcePolicy next;
    next.source_id = "src2";
    next.epoch = core::SourceEpoch{8};
    next.config_version = "v13";
    next.write_enabled = true;
    ASSERT_TRUE(core::is_ok(service().authorizePoint(powerAddress(), next, ctx())));

    auto old_source = sample(11.0, 6, now_);
    auto denied = service().updatePoint(powerAddress(), old_source, ctx());
    ASSERT_FALSE(core::is_ok(denied));
    EXPECT_EQ(core::ErrorCode::PermissionDenied, core::error_of(denied).code);

    auto fresh = sample(42.0, 1, now_);
    fresh.source_id = "src2";
    fresh.source_epoch = core::SourceEpoch{8};
    auto applied = service().updatePoint(powerAddress(), fresh, ctx());
    ASSERT_TRUE(core::is_ok(applied));
    EXPECT_EQ(SampleResult::Applied, core::value_of(applied));

    auto read = service().getPoint(powerAddress(), ctx());
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_DOUBLE_EQ(42.0, std::get<double>(*core::value_of(read).value));
    EXPECT_TRUE(core::value_of(read).source_authorized);
}

TEST_F(RealtimeServiceTest, DisabledPointIsNotWritable) {
    auto built = buildRuntimeCatalog(makeCatalog(false), makeOptions());
    ASSERT_TRUE(built.ok());
    auto catalog = std::make_shared<const RuntimeCatalog>(built.catalog);
    RealtimeService service(store_, catalog, [this] { return now_; });
    ASSERT_TRUE(core::is_ok(service.createPoint(powerAddress(), ctx())));
    auto rejected = service.updatePoint(powerAddress(), sample(1.0, 1, now_), ctx());
    ASSERT_FALSE(core::is_ok(rejected));
    EXPECT_EQ(core::ErrorCode::NotReady, core::error_of(rejected).code);
}

TEST_F(RealtimeServiceTest, MarksStaleAfterConfiguredThreshold) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    ASSERT_TRUE(core::is_ok(service().updatePoint(powerAddress(), sample(5.0, 1, now_), ctx())));
    now_ += 120LL * 1000 * 1000;
    auto read = service().getPoint(powerAddress(), ctx());
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_TRUE(core::value_of(read).stale);
    EXPECT_EQ(model::Quality::Good, core::value_of(read).quality.code);
}

TEST_F(RealtimeServiceTest, BatchWriteKeepsPerItemResults) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    std::vector<PointUpdate> updates;
    updates.push_back({powerAddress(), sample(3.0, 1, now_)});
    updates.push_back({PointAddress{Scope{ScopeKind::Unit, "unit01"}, {"pcs01"}, "unknown"},
                       sample(4.0, 1, now_)});
    auto results = service().updatePoints(updates, ctx());
    ASSERT_EQ(2u, results.size());
    ASSERT_TRUE(core::is_ok(results[0]));
    EXPECT_EQ(SampleResult::Applied, core::value_of(results[0]));
    ASSERT_FALSE(core::is_ok(results[1]));
    EXPECT_EQ(core::ErrorCode::NotFound, core::error_of(results[1]).code);
}

TEST_F(RealtimeServiceTest, CapabilityRequiresFreshAuthorizedGoodSamples) {
    ASSERT_TRUE(core::is_ok(service().createPoint(powerAddress(), ctx())));
    ASSERT_TRUE(core::is_ok(service().createPoint(runAddress(), ctx())));

    auto power = service().updatePoint(powerAddress(), sample(50.0, 1, now_), ctx());
    ASSERT_TRUE(core::is_ok(power));
    model::PointSample run;
    run.value = model::PointValue{std::string("running")};
    run.quality.code = model::Quality::Good;
    run.sequence = 1;
    run.source_epoch = core::SourceEpoch{7};
    run.source_id = "src1";
    run.config_version = "v13";
    run.source_time = now_;
    ASSERT_TRUE(core::is_ok(service().updatePoint(runAddress(), run, ctx())));

    auto available = service().resolveCapability(core::DeviceId{"dev1"}, "monitor_power", ctx());
    ASSERT_TRUE(core::is_ok(available));
    EXPECT_TRUE(core::value_of(available).available);

    now_ += 120LL * 1000 * 1000;
    auto stale = service().resolveCapability(core::DeviceId{"dev1"}, "monitor_power", ctx());
    ASSERT_TRUE(core::is_ok(stale));
    EXPECT_FALSE(core::value_of(stale).available);
    const auto& reasons = core::value_of(stale).reasons;
    EXPECT_NE(reasons.end(), std::find(reasons.begin(), reasons.end(), "stale"));
    for (const auto& check : core::value_of(stale).points) {
        EXPECT_FALSE(check.ok);
    }
}

TEST_F(RealtimeServiceTest, CapabilityReportsUnknownAndDisabled) {
    auto unknown = service().resolveCapability(core::DeviceId{"dev1"}, "missing", ctx());
    ASSERT_TRUE(core::is_ok(unknown));
    EXPECT_FALSE(core::value_of(unknown).available);
    ASSERT_EQ(1u, core::value_of(unknown).reasons.size());
    EXPECT_EQ("capability_not_configured", core::value_of(unknown).reasons.front());

    auto missing_device =
        service().resolveCapability(core::DeviceId{"nope"}, "monitor_power", ctx());
    ASSERT_FALSE(core::is_ok(missing_device));
    EXPECT_EQ(core::ErrorCode::NotFound, core::error_of(missing_device).code);
}

TEST_F(RealtimeServiceTest, DeviceStateOrdersSamplesAndComputesFreshness) {
    DeviceStateSample online;
    online.state = DeviceRunState::Online;
    online.quality.code = model::Quality::Good;
    online.since = now_ - 5000000;
    online.last_valid_time = now_ - 1000000;
    online.config_version = "v13";
    online.source_id = "src1";
    online.source_epoch = core::SourceEpoch{7};
    online.sequence = 1;
    ASSERT_TRUE(core::is_ok(service().updateDeviceState(core::DeviceId{"dev1"}, online, ctx())));

    auto read = service().getDeviceState(core::DeviceId{"dev1"}, ctx());
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_EQ(DeviceRunState::Online, core::value_of(read).sample.state);
    EXPECT_FALSE(core::value_of(read).stale);
    EXPECT_TRUE(core::value_of(read).config_current);

    auto older = online;
    older.sequence = 0;
    older.last_valid_time = now_;
    auto rejected = service().updateDeviceState(core::DeviceId{"dev1"}, older, ctx());
    ASSERT_FALSE(core::is_ok(rejected));
    EXPECT_EQ(core::ErrorCode::Conflict, core::error_of(rejected).code);

    now_ += 120LL * 1000 * 1000;
    auto stale = service().getDeviceState(core::DeviceId{"dev1"}, ctx());
    ASSERT_TRUE(core::is_ok(stale));
    EXPECT_TRUE(core::value_of(stale).stale);
}

TEST_F(RealtimeServiceTest, ChannelHealthAccumulatesAndResetsWindow) {
    ChannelHealthDelta first;
    first.state = ChannelRunState::Connected;
    first.observed_at = now_;
    first.requests = 10;
    first.successes = 9;
    first.timeouts = 1;
    first.average_latency_ms = 20.0;
    first.max_latency_ms = 30.0;
    first.config_version = "v13";
    first.source_id = "src1";
    first.source_epoch = core::SourceEpoch{7};
    first.sequence = 1;
    auto applied = service().reportChannelHealth(core::ChannelId{"ch01"}, first, ctx());
    ASSERT_TRUE(core::is_ok(applied));
    EXPECT_EQ(10u, core::value_of(applied).sample.requests);

    auto second = first;
    second.sequence = 2;
    second.observed_at = now_ + 1000000;
    second.requests = 10;
    second.successes = 10;
    second.timeouts = 0;
    second.average_latency_ms = 40.0;
    second.max_latency_ms = 40.0;
    auto merged = service().reportChannelHealth(core::ChannelId{"ch01"}, second, ctx());
    ASSERT_TRUE(core::is_ok(merged));
    EXPECT_EQ(20u, core::value_of(merged).sample.requests);
    EXPECT_EQ(19u, core::value_of(merged).sample.successes);
    EXPECT_DOUBLE_EQ(19.0 / 20.0, core::value_of(merged).success_rate);
    EXPECT_DOUBLE_EQ(30.0, core::value_of(merged).sample.average_latency_ms);
    EXPECT_DOUBLE_EQ(40.0, core::value_of(merged).sample.max_latency_ms);

    auto reset = second;
    reset.sequence = 3;
    reset.observed_at = now_ + 6LL * 60 * 1000 * 1000;
    reset.requests = 5;
    reset.successes = 5;
    reset.average_latency_ms = 12.0;
    auto fresh = service().reportChannelHealth(core::ChannelId{"ch01"}, reset, ctx());
    ASSERT_TRUE(core::is_ok(fresh));
    EXPECT_EQ(5u, core::value_of(fresh).sample.requests);
    EXPECT_DOUBLE_EQ(1.0, core::value_of(fresh).success_rate);
    EXPECT_DOUBLE_EQ(12.0, core::value_of(fresh).sample.average_latency_ms);
}

TEST_F(RealtimeServiceTest, ScopeRuntimeRequiresMatchingRevision) {
    ScopeRuntime runtime;
    runtime.config_version = "v13";
    runtime.activation_revision = "6";
    runtime.applied_at = now_;
    auto created = service().installScopeRuntime(runtime, std::nullopt, ctx());
    ASSERT_TRUE(core::is_ok(created));
    const Revision revision = core::value_of(created);

    auto read = service().getScopeRuntime(ctx());
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_EQ("v13", core::value_of(read).config_version);

    auto conflict = service().installScopeRuntime(runtime, std::nullopt, ctx());
    ASSERT_FALSE(core::is_ok(conflict));
    EXPECT_EQ(core::ErrorCode::Conflict, core::error_of(conflict).code);

    runtime.config_version = "v14";
    runtime.activation_revision = "7";
    auto switched = service().installScopeRuntime(runtime, revision, ctx());
    ASSERT_TRUE(core::is_ok(switched));

    auto stale_switch = service().installScopeRuntime(runtime, revision, ctx());
    ASSERT_FALSE(core::is_ok(stale_switch));
    EXPECT_EQ(core::ErrorCode::Conflict, core::error_of(stale_switch).code);
}

}  // namespace
}  // namespace ems::rtdb
