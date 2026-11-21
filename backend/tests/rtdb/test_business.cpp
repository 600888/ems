#include <gtest/gtest.h>
#include <rtdb/backend/shared_memory_store.h>
#include <rtdb/client/command_client.h>
#include <rtdb/client/point_client.h>
#include <rtdb/client/realtime_client.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

#include "store_detail.h"

namespace ems::rtdb {
namespace {
#ifdef EMS_REALTIME_TEST_SHM
TEST(CommandProcess, SubmissionAndResultAreVisibleAcrossProcesses) {
    const auto segment = std::string("command_process_") + std::getenv("EMS_TEST_RUN_ID");
    const auto path = std::filesystem::temp_directory_path() / (segment + ".json");

    struct Cleanup {
        std::string segment;
        std::filesystem::path path;

        ~Cleanup() {
            SharedMemoryStore::remove(segment);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{segment, path};

    std::ofstream(path) << "{\"realtime\":{\"backend\":{\"type\":\"shm\",\"options\":{"
                           "\"segment_name\":\""
                        << segment
                        << "\",\"capacity\":128,\"shard_count\":1,\"segment_bytes\":1048576}}}}";
    const auto context = core::make_call_context(std::chrono::seconds(5));
    auto owner = RealtimeClient::initialize(path.u8string(), context);
    ASSERT_TRUE(core::is_ok(owner));
    ASSERT_EXIT(
        {
            auto opened =
                CommandClient::open(path.u8string(), {ScopeKind::Station, "station"}, "", context);
            if (!core::is_ok(opened)) std::_Exit(1);
            const auto deadline = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count() +
                                  10000;
            auto submitted = core::value_of(opened)->submitCommand(
                {"cross-process", "setpoint", 42, 7, deadline}, context);
            if (!core::is_ok(submitted)) std::_Exit(2);
            auto record = core::value_of(submitted).record;
            record.status = "Acked";
            record.reason = "child_process_result";
            std::_Exit(core::is_ok(core::value_of(opened)->updateCommand(record, context)) ? 0 : 3);
        },
        ::testing::ExitedWithCode(0), "");
    auto peer = CommandClient::open(path.u8string(), {ScopeKind::Station, "station"}, "", context);
    ASSERT_TRUE(core::is_ok(peer));
    auto record = core::value_of(peer)->readCommand("cross-process", context);
    ASSERT_TRUE(core::is_ok(record));
    EXPECT_EQ(core::value_of(record).status, "Acked");
    EXPECT_EQ(core::value_of(record).reason, "child_process_result");
    EXPECT_DOUBLE_EQ(core::value_of(record).request.value, 42);
}

class PointIntegration : public ::testing::Test {
   protected:
    std::filesystem::path path;
    std::unique_ptr<RealtimeClient> owner;
    std::shared_ptr<const RuntimeCatalog> catalog;
    std::shared_ptr<PointClient> writer, reader;

    CallContext context() { return core::make_call_context(std::chrono::seconds(3)); }

    void SetUp() override {
        auto token = detail::token();
        ASSERT_TRUE(core::is_ok(token));
        const auto name = "typed_points_" + core::value_of(token);
        path = std::filesystem::temp_directory_path() / (name + ".json");
        std::ofstream(path)
            << "{\"realtime\":{\"backend\":{\"type\":\"shm\",\"options\":{\"segment_name\":\""
            << name << "\"}}}}";
        auto opened = RealtimeClient::initialize(path.u8string(), context());
        ASSERT_TRUE(core::is_ok(opened));
        owner = std::move(std::get<0>(opened));
        auto definitions =
            std::make_shared<RuntimeCatalog>(Scope{ScopeKind::Station, "station"}, "7", "", 60000);
        for (auto type : {model::PointType::YC, model::PointType::YX, model::PointType::YK,
                          model::PointType::YT}) {
            RuntimePoint point;
            point.point_id =
                core::PointId{std::string("point") + std::to_string(static_cast<int>(type))};
            point.address = {{ScopeKind::Station, "station"},
                             {"pcs"},
                             std::string("code") + std::to_string(static_cast<int>(type))};
            point.point_type = type;
            point.kind = model::to_point_kind(type);
            point.value_type = (type == model::PointType::YX || type == model::PointType::YK)
                                   ? model::ValueType::Bool
                                   : model::ValueType::Double;
            ASSERT_TRUE(definitions->addPoint(point));
        }
        catalog = definitions;
        auto first = PointClient::open(path.u8string(), catalog, context());
        ASSERT_TRUE(core::is_ok(first));
        writer = core::value_of(first);
        ASSERT_TRUE(core::is_ok(writer->installWriter(context())));
        auto second = PointClient::open(path.u8string(), catalog, context());
        ASSERT_TRUE(core::is_ok(second));
        reader = core::value_of(second);
    }

    void TearDown() override {
        writer.reset();
        reader.reset();
        owner.reset();
        RealtimeClient::cleanup(path.u8string(), context());
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

TEST_F(PointIntegration, CollectorReaderAndTypedFacadeUseOneRecord) {
    ASSERT_TRUE(core::is_ok(writer->update("point0", 42.5, model::Quality::Good, 1234, context())));
    auto observed = reader->readPointJson("yc", "point0", context());
    ASSERT_TRUE(core::is_ok(observed));
    const auto json = nlohmann::json::parse(core::value_of(observed));
    EXPECT_EQ(json["value"], 42.5);
    auto connection = RealtimeClient::open(path.u8string(), context());
    ASSERT_TRUE(core::is_ok(connection));
    std::shared_ptr<IRealtimeStore> store = std::move(std::get<0>(connection));
    RealtimeService service(store, catalog);
    auto snapshot =
        service.getPoint(catalog->findPoint(core::PointId{"point0"})->address, context());
    ASSERT_TRUE(core::is_ok(snapshot));
    EXPECT_EQ(std::get<double>(*core::value_of(snapshot).value), 42.5);
    EXPECT_EQ(
        core::error_of(reader->update("point0", 2, model::Quality::Good, 1235, context())).code,
        ErrorCode::PermissionDenied);
}

TEST_F(PointIntegration, CommandProjectionChecksAuthorizationBeforeAcceptingDuplicate) {
    CommandRecord command;
    command.request = {"first", "point3", 42, 7, 9999999999999};
    command.status = "Acked";
    command.updated_at_ms = 1234;
    ASSERT_TRUE(core::is_ok(writer->projectCommand(command, 1, context())));
    const auto address = catalog->findPoint(core::PointId{"point3"})->address;
    const RecordKey key{address.scope, RecordKind::Point, address.device_path, address.code};
    auto record = owner->read(key, context());
    ASSERT_TRUE(core::is_ok(record));
    auto changed = core::value_of(record);
    changed.fields["accepted_source_id"] = "blocked";
    ASSERT_TRUE(
        core::is_ok(owner->compareExchange({key, changed.fields, changed.revision}, context())));
    auto duplicate = writer->projectCommand(command, 1, context());
    ASSERT_FALSE(core::is_ok(duplicate));
    EXPECT_EQ(core::error_of(duplicate).code, ErrorCode::PermissionDenied);
    ASSERT_TRUE(core::is_ok(writer->installWriter(context())));
    auto reopened = PointClient::open(path.u8string(), catalog, context());
    ASSERT_TRUE(core::is_ok(reopened));
    auto replacement = core::value_of(reopened);
    ASSERT_TRUE(core::is_ok(replacement->installWriter(context())));
    ASSERT_TRUE(core::is_ok(replacement->projectCommand(command, 1, context())));
    auto observed = reader->readPointJson("yt", "point3", context());
    ASSERT_TRUE(core::is_ok(observed));
    EXPECT_EQ(nlohmann::json::parse(core::value_of(observed))["quality"], 0);
}

TEST_F(PointIntegration, CommandProjectionCannotOverwriteNewerCommandAndKeepsFeedbackSeparate) {
    CommandRecord command;
    command.request = {"first", "point3", 42, 7, 9999999999999};
    command.status = "Acked";
    command.updated_at_ms = 1234;
    ASSERT_TRUE(core::is_ok(writer->projectCommand(command, 1, context())));
    command.request.command_id = "second";
    command.request.value = 84;
    command.updated_at_ms = 1235;
    ASSERT_TRUE(core::is_ok(writer->projectCommand(command, 2, context())));
    command.request.command_id = "first";
    command.request.value = 42;
    command.status = "Succeeded";
    command.updated_at_ms = 1236;
    EXPECT_EQ(core::value_of(writer->projectCommand(command, 1, context())),
              SampleResult::IgnoredOutOfOrder);
    auto actual = reader->readPointJson("yt", "point3", context());
    ASSERT_TRUE(core::is_ok(actual));
    const auto json = nlohmann::json::parse(core::value_of(actual));
    EXPECT_EQ(json["value"], 84);
    EXPECT_EQ(json["command_id"], "second");
    EXPECT_EQ(json["value_role"], "command_target");
    EXPECT_FALSE(core::is_ok(reader->readPointJson("yc", "point0", context())));
}
#endif
}  // namespace
}  // namespace ems::rtdb
