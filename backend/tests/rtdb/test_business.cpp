#include <gtest/gtest.h>
#include <rtdb/backend/shared_memory_store.h>
#include <rtdb/client/realtime_client.h>
#include <rtdb/client/rtdb_ems.h>
#include <rtdb/client/rtdb_manager.h>

#include <filesystem>
#include <fstream>
#include <thread>

#include "store_detail.h"

namespace ems::rtdb {
namespace {
#ifdef EMS_REALTIME_TEST_SHM
class BusinessTest : public ::testing::Test {
   protected:
    RtDbConfig config;
    std::string segment;
    std::filesystem::path path;
    std::unique_ptr<RealtimeClient> owner;

    void SetUp() override {
        auto nonce = detail::token();
        ASSERT_TRUE(core::is_ok(nonce));
        segment = "business_" + core::value_of(nonce);
        path = std::filesystem::temp_directory_path() / (segment + ".json");
        std::ofstream(path) << "{\"realtime\":{\"backend\":{\"type\":\"shm\",\"options\":{"
                               "\"segment_name\":\""
                            << segment
                            << "\",\"capacity\":128,"
                               "\"shard_count\":1,\"segment_bytes\":1048576}}}}";
        config.config_path = path.u8string();
        auto opened = RealtimeClient::initialize(config.config_path,
                                                 core::make_call_context(std::chrono::seconds(3)));
        ASSERT_TRUE(core::is_ok(opened));
        owner = std::get<0>(std::move(opened));
    }

    void TearDown() override {
        owner.reset();
        EXPECT_TRUE(core::is_ok(SharedMemoryStore::remove(segment)));
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

TEST_F(BusinessTest, FourPointTypesRoundTripWholeModelsAndCatalog) {
    RtDb writer, reader;
    ASSERT_TRUE(writer.initialize(config));
    ASSERT_TRUE(reader.initialize(config));
    YcPoint yc;
    yc.value_ = 123.123456789;
    yc.timestamp_ = 1234567890123ULL;
    yc.name_ = "active power";
    yc.unit_ = "kW";
    ASSERT_TRUE(writer.setYc("PCS.Power:1", yc));
    YcPoint actual;
    ASSERT_TRUE(reader.getYc("PCS.Power:1", actual));
    EXPECT_DOUBLE_EQ(actual.value_, yc.value_);
    EXPECT_EQ(actual.timestamp_, yc.timestamp_);
    EXPECT_EQ(actual.name_, yc.name_);
    EXPECT_EQ(actual.unit_, yc.unit_);
    EXPECT_EQ(actual.code_, "PCS.Power:1");
    YxPoint yx;
    yx.value_ = 1;
    ASSERT_TRUE(writer.setYx("breaker", yx));
    YxPoint readYx;
    ASSERT_TRUE(reader.getYx("breaker", readYx));
    EXPECT_EQ(readYx.value_, 1);
    YkPoint yk;
    yk.status_ = YKStatus::Success;
    yk.executeTime_ = 888;
    yk.result_ = "done";
    ASSERT_TRUE(writer.setYk("start", yk));
    YkPoint readYk;
    ASSERT_TRUE(reader.getYk("start", readYk));
    EXPECT_EQ(readYk.status_, YKStatus::Success);
    EXPECT_EQ(readYk.executeTime_, 888);
    YtPoint yt;
    yt.value_ = 12.5;
    ASSERT_TRUE(writer.setYt("target", yt));
    YtPoint readYt;
    ASSERT_TRUE(reader.getYt("target", readYt));
    EXPECT_DOUBLE_EQ(readYt.value_, 12.5);
    EXPECT_EQ(reader.getYcCodes("PCS.*"), std::vector<std::string>{"PCS.Power:1"});
    ASSERT_TRUE(reader.deleteYc("PCS.Power:1"));
    EXPECT_TRUE(writer.getYcCodes().empty());
    auto isolated = config;
    isolated.prefix = "another:";
    RtDb other;
    ASSERT_TRUE(other.initialize(isolated));
    EXPECT_TRUE(other.keys("*").empty());
}

TEST_F(BusinessTest, SnapshotsTopologyAndConfigCacheUseUnifiedLibrary) {
    RTDBEMS writer, reader;
    ASSERT_TRUE(writer.initialize(config));
    ASSERT_TRUE(reader.initialize(config));
    DeviceSnapshot snapshot;
    snapshot.DeviceType = "PCS";
    snapshot.ycValues["power"] = 42;
    snapshot.metadata["obsolete"] = "gone";
    ASSERT_TRUE(writer.setDeviceSnapshot("pcs01", snapshot));
    snapshot.metadata.clear();
    ASSERT_TRUE(writer.setDeviceSnapshot("pcs01", snapshot));
    DeviceSnapshot actual;
    ASSERT_TRUE(reader.getDeviceSnapshot("pcs01", actual));
    EXPECT_DOUBLE_EQ(actual.ycValues.at("power"), 42);
    EXPECT_TRUE(actual.metadata.empty());
    SystemTopology topology;
    topology.topologyId = "station01";
    topology.deviceIds = {"pcs01", "bms01"};
    ASSERT_TRUE(writer.setTopology(topology));
    SystemTopology read;
    ASSERT_TRUE(reader.getTopology(topology.topologyId, read));
    EXPECT_EQ(read.deviceIds, topology.deviceIds);
    RtDbManager manager;
    ASSERT_TRUE(manager.initialize(config));
    RtDbConfigCache::YcConfig point;
    point.code = "power";
    point.name = "power";
    manager.getConfigCache()->loadYcConfigs({point});
    YcPoint runtime;
    runtime.value_ = 7;
    ASSERT_TRUE(manager.updateYc("power", runtime));
    RtDbPointManager::YcFullPoint full;
    ASSERT_TRUE(manager.getYcFull("power", full));
    EXPECT_EQ(full.config.code, "power");
    EXPECT_DOUBLE_EQ(full.data.value_, 7);
}

TEST_F(BusinessTest, ConcurrentUpdatesAndOwnerComparisonDoNotLoseWrites) {
    RtDb first, second;
    ASSERT_TRUE(first.initialize(config));
    ASSERT_TRUE(second.initialize(config));
    std::atomic<bool> ok{true};
    auto increment = [&](RtDb& client) {
        for (int i = 0; i < 20; ++i) {
            int64_t value = 0;
            if (!client.incr("counter", value)) ok = false;
        }
    };
    std::thread worker([&] { increment(first); });
    increment(second);
    worker.join();
    EXPECT_TRUE(ok);
    int count = 0;
    ASSERT_TRUE(first.get("counter", count));
    EXPECT_EQ(count, 40);
    ASSERT_TRUE(first.setIfAbsentWithExpire("lock", "owner-long", 10));
    EXPECT_FALSE(second.setIfAbsentWithExpire("lock", "other", 10));
    EXPECT_FALSE(second.deleteIfValue("lock", "owner"));
    ASSERT_TRUE(first.deleteIfValue("lock", "owner-long"));
    EXPECT_TRUE(second.setIfAbsentWithExpire("lock", "other", 10));
    EXPECT_FALSE(first.deleteIfValue("lock", "owner-long"));
    EXPECT_TRUE(second.exists("lock"));
}

TEST_F(BusinessTest, PartialHashUpdatesPreserveFieldsAndBatchReportsFailure) {
    RtDb client;
    ASSERT_TRUE(client.initialize(config));
    ASSERT_TRUE(client.setHash("metadata", {{"a", "1"}, {"b", "2"}}));
    ASSERT_TRUE(client.setHashField("metadata", "a", "3"));
    Fields fields;
    ASSERT_TRUE(client.getHash("metadata", fields));
    EXPECT_EQ(fields.at("b"), "2");
    ASSERT_TRUE(client.replaceHash("metadata", {{"c", "4"}}));
    ASSERT_TRUE(client.getHash("metadata", fields));
    EXPECT_EQ(fields, (Fields{{"c", "4"}}));
    std::map<std::string, YcPoint> batch{{"valid", {}}, {std::string(300, 'x'), {}}};
    EXPECT_FALSE(client.batchSetYc(batch));
    YcPoint point;
    EXPECT_TRUE(client.getYc("valid", point));
}
#endif
}  // namespace
}  // namespace ems::rtdb
