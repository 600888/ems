#include <gtest/gtest.h>
#include <rtdb/backend/shared_memory_store.h>
#include <rtdb/client/realtime_client.h>
#include <rtdb/client/rtdb.h>

#include <filesystem>
#include <fstream>

#include "net/collect/data_dispatcher.h"

#ifdef EMS_REALTIME_TEST_SHM
TEST(RtdbDispatcher, WritesThroughUnifiedLibraryWithoutRedisNotificationDependency) {
    using namespace ems;
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto segment = "dispatcher_" + nonce;
    const auto path = std::filesystem::temp_directory_path() / (segment + ".json");

    struct Cleanup {
        std::string segment;
        std::filesystem::path path;

        ~Cleanup() {
            rtdb::SharedMemoryStore::remove(segment);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{segment, path};

    std::ofstream(path) << "{\"realtime\":{\"backend\":{\"type\":\"shm\",\"options\":{"
                           "\"segment_name\":\""
                        << segment
                        << "\",\"capacity\":128,"
                           "\"shard_count\":1,\"segment_bytes\":1048576}}}}";
    const auto ctx = core::make_call_context(std::chrono::seconds(3));
    auto owner = rtdb::RealtimeClient::initialize(path.string(), ctx);
    ASSERT_TRUE(core::is_ok(owner));
    net::collect::DataDispatcherConfig config;
    config.rtdbConfigPath_ = path.string();
    config.notificationBackend_ = net::collect::NotificationBackend::None;
    // Deliberately unusable notification address: storage must not depend on it.
    config.redisHost_ = "invalid.invalid";
    config.redisPort_ = 1;
    net::collect::DataDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.initialize(config));
    ASSERT_TRUE(dispatcher.update_yc("power", 42.5, 0, 1234));
    ASSERT_TRUE(dispatcher.update_yx("breaker", 1, 0, 1234));
    rtdb::RtDbConfig readerConfig;
    readerConfig.config_path = path.string();
    rtdb::RtDb reader;
    ASSERT_TRUE(reader.initialize(readerConfig));
    rtdb::YcPoint point;
    ASSERT_TRUE(reader.getYc("power", point));
    EXPECT_DOUBLE_EQ(point.value_, 42.5);
    EXPECT_EQ(point.timestamp_, 1234);
    rtdb::YxPoint status;
    ASSERT_TRUE(reader.getYx("breaker", status));
    EXPECT_EQ(status.value_, 1);
    net::collect::PointValue analog;
    analog.code = "YX_analog-stable-id";
    analog.value = 6.25;
    analog.quality = 0;
    analog.timestamp = 2345;
    analog.details = {{"point_type", "yc"}};
    net::collect::PointValue discrete = analog;
    discrete.code = "stable-discrete-id";
    discrete.value = 1;
    discrete.details = {{"point_type", "yx"}};
    ASSERT_EQ(dispatcher.batch_update({analog, discrete}), 2);
    ASSERT_TRUE(reader.getYc(analog.code, point));
    EXPECT_DOUBLE_EQ(point.value_, 6.25);
    ASSERT_TRUE(reader.getYx(discrete.code, status));
    EXPECT_EQ(status.value_, 1);
    dispatcher.shutdown();
    EXPECT_TRUE(reader.getYc("power", point));
}
#endif
