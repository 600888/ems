#include <gtest/gtest.h>
#include <rtdb/backend/shared_memory_store.h>
#include <rtdb/client/point_client.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "net/collect/data_dispatcher.h"
#ifdef EMS_REALTIME_TEST_SHM
TEST(RtdbDispatcher, WritesCanonicalPointsWithoutLegacyCache) {
    using namespace ems;
    const auto name =
        "dispatcher_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / (name + ".json");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            rtdb::RealtimeClient::cleanup(path.string(),
                                          core::make_call_context(std::chrono::seconds(2)));
            std::filesystem::remove(path);
        }
    } cleanup{path};

    std::ofstream(path)
        << nlohmann::json{{"realtime",
                           {{"backend", {{"type", "shm"}, {"options", {{"segment_name", name}}}}}}}}
               .dump();
    const auto ctx = core::make_call_context(std::chrono::seconds(3));
    auto owner = rtdb::RealtimeClient::initialize(path.string(), ctx);
    ASSERT_TRUE(core::is_ok(owner));
    auto catalog = std::make_shared<rtdb::RuntimeCatalog>(
        rtdb::Scope{rtdb::ScopeKind::Station, "station001"}, "1", "", 60000);
    rtdb::RuntimePoint point;
    point.point_id = core::PointId{"power"};
    point.address = {{rtdb::ScopeKind::Station, "station001"}, {"pcs"}, "power"};
    ASSERT_TRUE(catalog->addPoint(point));
    net::collect::DataDispatcherConfig options;
    options.rtdbConfigPath_ = path.string();
    net::collect::DataDispatcher dispatcher;
    ASSERT_TRUE(dispatcher.initialize(options));
    ASSERT_TRUE(dispatcher.configure(catalog));
    ASSERT_TRUE(dispatcher.update_yc("power", 42.5, 0, 1234));
    auto opened = rtdb::PointClient::open(path.string(), catalog, ctx);
    ASSERT_TRUE(core::is_ok(opened));
    auto read = core::value_of(opened)->readPointJson("yc", "power", ctx);
    ASSERT_TRUE(core::is_ok(read));
    EXPECT_EQ(nlohmann::json::parse(core::value_of(read))["value"], 42.5);
}
#endif
