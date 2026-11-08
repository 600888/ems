#include <common/core/time.h>
#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <miniz.h>
#include <sqlite3.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <ormpp/sqlite.hpp>
#include <util/excel.hpp>

#include "common/db/configuration/orm_device_management_store.h"
#include "common/db/runtime/transaction_database.h"
#include "web/app/module_registry.h"
#include "web/app/web_options.h"
#include "web/modules/device/application/device_service.h"
#include "web/modules/identity/application/session_service.h"
#include "web/modules/runtime/runtime_service.h"
#include "web/platform/crypto.h"
#include "web/platform/executor/bounded_executor.h"

namespace ems::web {
using namespace database;

namespace {
class TestConnection final : public detail::Connection {
   public:
    ormpp::sqlite orm;
    inline static std::atomic<std::size_t> operation_count{0};

    core::Status prepare(const core::CallContext& ctx) {
        ++operation_count;
        return core::has_expired(ctx)
                   ? core::Status{core::make_error(core::ErrorCode::DeadlineExceeded,
                                                   core::WriteEffect::NotApplied)}
                   : core::ok_status();
    }

    core::Status begin(const core::CallContext&) override { return status(orm.begin()); }

    core::Status commit(const core::CallContext&) override { return status(orm.commit()); }

    core::Status rollback(const core::CallContext&) override { return status(orm.rollback()); }

    static core::Status status(bool ok) {
        return ok ? core::ok_status()
                  : core::Status{
                        core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied)};
    }
};

class WebDevice : public ::testing::Test {
   protected:
    std::filesystem::path directory;
    MySqlDatabaseContext db;
    std::shared_ptr<detail::TransactionDatabase> runtime;
    std::unique_ptr<DeviceService> service;
    Principal actor{"operator", {"*"}, {"*"}, {"*"}};

    core::CallContext context() { return core::make_call_context(std::chrono::seconds(30)); }

    void sql(const std::string& command) {
        sqlite3* connection = nullptr;
        ASSERT_EQ(sqlite3_open((directory / "config.db").u8string().c_str(), &connection),
                  SQLITE_OK);
        char* error = nullptr;
        ASSERT_EQ(sqlite3_exec(connection, command.c_str(), nullptr, nullptr, &error), SQLITE_OK)
            << (error ? error : "");
        sqlite3_free(error);
        sqlite3_close(connection);
    }

    void SetUp() override {
        directory = std::filesystem::temp_directory_path() / random_token();
        std::filesystem::create_directories(directory);
        std::ifstream input(EMS_WEB_SCHEMA_FILE);
        std::string schema((std::istreambuf_iterator<char>(input)), {});
        sql(schema);
        const auto file = (directory / "config.db").u8string();
        runtime = std::make_shared<detail::TransactionDatabase>(
            detail::TransactionOptions{},
            [file](const core::CallContext&) -> core::Result<detail::ConnectionLease> {
                auto connection = std::make_shared<TestConnection>();
                if (!connection->orm.connect("", "", "", file, {}, {}))
                    return core::make_error(core::ErrorCode::Unavailable,
                                            core::WriteEffect::NotApplied);
                connection->orm.set_enable_transaction(false);
                return detail::ConnectionLease{connection,
                                               [](std::shared_ptr<detail::Connection>, bool) {}};
            });
        auto acquire = [runtime = runtime](
                           ITransaction& tx,
                           const core::CallContext& ctx) -> core::Result<TestConnection*> {
            auto result = runtime->connection(tx, ctx);
            if (!core::is_ok(result)) return core::error_of(result);
            return static_cast<TestConnection*>(core::value_of(result));
        };
        auto invalidate = [runtime = runtime](ITransaction& tx) { runtime->invalidate(tx); };
        auto failure = [](TestConnection&) {
            return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "SQL fixture failure");
        };
        db.database = runtime;
        db.device_management = std::make_shared<detail::OrmDeviceManagementStore<TestConnection>>(
            acquire, invalidate, failure, false);
        db.device_config = std::make_shared<detail::OrmDeviceConfigRepository<TestConnection>>(
            acquire, invalidate, failure);

        auto tx = take(db.database->begin(context()));
        CfgDeviceRow root;
        root.device_id = "station";
        root.node_kind = "station";
        root.code = "station";
        root.name = "Station";
        root.path = "/";
        take(db.device_management->put(*tx, root, context()));
        take(tx->commit(context()));
        sql("INSERT INTO "
            "cfg_device_type(type_code,type_version,name,category,status,description,created_at,"
            "updated_at) VALUES('hvac','hvac-v1','HVAC','hvac','active','','','')");
        service = std::make_unique<DeviceService>(db, (directory / "upload").u8string());
    }

    void TearDown() override {
        service.reset();
        db = {};
        runtime.reset();
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    DeviceResult execute(const std::string& action, const Json& body) {
        return service->execute(action, body, actor, context());
    }

    Json create(const std::string& id = "hvac01", const std::string& revision = "0") {
        return {{"device_id", id},
                {"expected_config_revision", revision},
                {"fields",
                 {{"parent_device_id", "station"},
                  {"node_kind", "device"},
                  {"code", id},
                  {"name", "HVAC"},
                  {"device_type_code", "hvac"},
                  {"type_version", "hvac-v1"},
                  {"enabled", false}}}};
    }

    void bind_protocol(const std::string& protocol, const std::string& address = "1") {
        execute("/api/devices/create", create());
        execute("/api/bindings/create", {{"device_id", "hvac01"},
                                         {"binding_id", "binding01"},
                                         {"expected_config_revision", "1"},
                                         {"fields",
                                          {{"new_channel",
                                            {{"channel_id", "channel01"},
                                             {"code", "channel01"},
                                             {"name", "Test"},
                                             {"protocol", protocol},
                                             {"conn_mode", "tcp_client"},
                                             {"endpoint", "127.0.0.1:502"}}},
                                           {"device_address", address},
                                           {"can_collect", true},
                                           {"can_control", true}}}});
    }

    void import_example(const std::string& protocol) {
        bind_protocol(protocol, protocol == "dlt645" ? "123456789012" : "1");
        auto path = std::filesystem::path(__FILE__)
                        .parent_path()
                        .parent_path()
                        .parent_path()
                        .parent_path() /
                    "data" / "point_csv" / ("point_sample_" + protocol + ".xlsx");
        std::ifstream input(path, std::ios::binary);
        ASSERT_TRUE(input.good()) << path;
        if (protocol == "modbus") {
            // 仓库示例混有从机 1/2。整站包每设备一文件，按真实绑定拆分，不能改从机号。
            util::Excel original;
            original.open(path);
            util::Excel selected;
            path = directory / "modbus_selected.xlsx";
            selected.create(path);
            for (const auto& sheet : original.sheetNames()) {
                selected.addSheet(sheet);
                const auto [rows, columns] = original.usedSize(sheet);
                unsigned target = 1;
                for (unsigned row = 1; row <= rows; ++row) {
                    if (row > 1 &&
                        original.readCell(sheet, row, 3) != util::ExcelValue(std::int64_t{1}))
                        continue;
                    selected.writeRange(sheet, target++, 1,
                                        original.readRange(sheet, row, 1, 1, columns));
                }
            }
            selected.removeSheet("Sheet1");
            selected.save();
            selected.close();
            input.close();
            input.open(path, std::ios::binary);
        }
        Upload upload;
        upload.filename = path.filename().u8string();
        upload.bytes.assign(std::istreambuf_iterator<char>(input), {});
        const auto preview = service->execute("/api/point-imports/create",
                                              {{"device_id", "hvac01"},
                                               {"binding_id", "binding01"},
                                               {"source_kind", "telemetry_xlsx"}},
                                              actor, context(), upload);
        ASSERT_EQ(preview.data.at("status"), "confirmed") << preview.data;
        auto id = preview.data.at("import_job_id");
        const auto applied = execute("/api/point-imports/apply",
                                     {{"import_job_id", id},
                                      {"expected_preview_hash", preview.data.at("preview_hash")},
                                      {"expected_config_revision", "2"},
                                      {"mode", "merge"}});
        EXPECT_GT(applied.data.at("created").get<int>(), 0);
        auto yc = execute("/api/points/list", {{"device_id", "hvac01"}, {"point_type", "yc"}});
        ASSERT_GT(yc.data.at("total").get<int>(), 0);
        const auto& point = yc.data.at("items").front();
        if (protocol == "iec104") {
            EXPECT_EQ(point.at("address").at("protocol_address").at("casdu"), 1);
            EXPECT_EQ(point.at("address").at("protocol_address").at("iec104_type"), "M_ME_NB_1");
            EXPECT_EQ(point.at("address").at("space"), nullptr);
        } else if (protocol == "modbus") {
            EXPECT_EQ(point.at("wire_type"), "i32");
            EXPECT_EQ(point.at("byte_order"), "ABCD");
        } else {
            EXPECT_TRUE(point.at("address").at("protocol_address").contains("data_id"));
        }
        if (protocol != "dlt645") {
            auto yk = execute("/api/points/list", {{"device_id", "hvac01"}, {"point_type", "yk"}});
            auto yt = execute("/api/points/list", {{"device_id", "hvac01"}, {"point_type", "yt"}});
            EXPECT_GT(yk.data.at("total").get<int>(), 0);
            EXPECT_GT(yt.data.at("total").get<int>(), 0);
            EXPECT_EQ(yk.data.at("items").front().at("enabled"), false);
            EXPECT_TRUE(yk.data.at("items")
                            .front()
                            .at("address")
                            .at("protocol_address")
                            .contains("feedback_key"));
        }
    }
};

TEST_F(WebDevice, TelemetryModbusExampleAppliesAllFourCategories) { import_example("modbus"); }

TEST_F(WebDevice, TelemetryIec104ExamplePreservesProtocolAddressOnApply) {
    import_example("iec104");
}

TEST_F(WebDevice, TelemetryDlt645ExampleUsesMeterBindingAndDataIdentifiers) {
    import_example("dlt645");
}

TEST_F(WebDevice, TelemetryIec104Keeps24BitIoaAndRejectsFormulaAndWrongAsdu) {
    bind_protocol("iec104");
    const auto file = directory / "ioa.xlsx";
    util::Excel excel;
    excel.create(file);
    excel.renameSheet("Sheet1", "遥测");
    excel.writeRange("遥测", 1, 1,
                     {{std::string("测点编码"), std::string("测点名称"), std::string("从机地址"),
                       std::string("寄存器地址"), std::string("104ASDU类型")},
                      {std::string("power"), std::string("有功功率"), std::int64_t{1},
                       std::int64_t{0xffffff}, std::string("M_ME_NC_1")}});
    auto preview = [&] {
        excel.save();
        std::ifstream input(file, std::ios::binary);
        Upload upload;
        upload.filename = "ioa.xlsx";
        upload.bytes.assign(std::istreambuf_iterator<char>(input), {});
        return service->execute("/api/point-imports/create",
                                {{"device_id", "hvac01"},
                                 {"binding_id", "binding01"},
                                 {"source_kind", "telemetry_xlsx"}},
                                actor, context(), upload);
    };
    auto result = preview();
    ASSERT_EQ(result.data.at("status"), "confirmed");
    auto applied = execute("/api/point-imports/apply",
                           {{"import_job_id", result.data.at("import_job_id")},
                            {"expected_preview_hash", result.data.at("preview_hash")},
                            {"expected_config_revision", "2"},
                            {"mode", "merge"}});
    auto points = execute("/api/points/list", {{"device_id", "hvac01"}, {"point_type", "yc"}});
    ASSERT_EQ(points.data.at("total"), 1);
    EXPECT_EQ(points.data.at("items").front().at("address").at("protocol_address").at("ioa"),
              0xffffff);
    excel.writeCell("遥测", 2, 4, std::int64_t{0x1000000});
    EXPECT_EQ(preview().data.at("status"), "failed");
    excel.writeCell("遥测", 2, 4, std::int64_t{1});
    excel.writeCell("遥测", 2, 5, std::string("M_SP_NA_1"));
    EXPECT_EQ(preview().data.at("status"), "failed");
    excel.writeCell("遥测", 2, 5, std::string("M_ME_NC_1"));
    excel.writeFormula("遥测", "D2", "1+1");
    EXPECT_EQ(preview().data.at("status"), "failed");
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        applied.meta.at("config_revision"));
}

std::string station_zip(const Json& manifest, const std::string& file, const std::string& bytes) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) throw std::runtime_error("zip init");
    const auto text = manifest.dump();
    const bool ok = mz_zip_writer_add_mem(&archive, "manifest.json", text.data(), text.size(), 0) &&
                    mz_zip_writer_add_mem(&archive, file.c_str(), bytes.data(), bytes.size(), 0);
    void* memory = nullptr;
    size_t size = 0;
    if (!ok || !mz_zip_writer_finalize_heap_archive(&archive, &memory, &size)) {
        mz_zip_writer_end(&archive);
        throw std::runtime_error("zip write");
    }
    std::string result(static_cast<char*>(memory), size);
    mz_free(memory);
    mz_zip_writer_end(&archive);
    return result;
}

TEST_F(WebDevice, StationTelemetrySharesRackTemplateWithIndependentSlaveBindings) {
    const auto file = directory / "rack.xlsx";
    util::Excel excel;
    excel.create(file);
    excel.renameSheet("Sheet1", "遥测");
    excel.writeRange("遥测", 1, 1,
                     {{std::string("测点编码"), std::string("测点名称"), std::string("从机地址"),
                       std::string("寄存器地址"), std::string("功能码"), std::string("解析码")},
                      {std::string("voltage"), std::string("簇电压"), std::string(""),
                       std::int64_t{0x200b}, std::int64_t{3}, std::string("UINT16_AB")}});
    excel.addSheet("遥调");
    excel.writeRange("遥调", 1, 1,
                     {{std::string("测点编码"), std::string("测点名称"), std::string("从机地址"),
                       std::string("寄存器地址"), std::string("功能码"), std::string("解析码")},
                      {std::string("voltage_set"), std::string("电压设定"), std::string(""),
                       std::int64_t{0x200b}, std::int64_t{6}, std::string("UINT16_AB")}});
    excel.save();
    excel.close();
    std::ifstream input(file, std::ios::binary);
    std::string bytes(std::istreambuf_iterator<char>(input), {});
    Json manifest = {{"schema", "ems-station-package/1"}, {"station", {{"code", "station"}}},
                     {"devices", Json::array()},          {"channels", Json::array()},
                     {"bindings", Json::array()},         {"point_files", Json::array()}};
    manifest["devices"].push_back({{"code", "hvac01"},
                                   {"name", "BMS"},
                                   {"node_kind", "device"},
                                   {"device_type_code", "hvac"},
                                   {"type_version", "hvac-v1"},
                                   {"enabled", 0}});
    for (int rack = 1; rack <= 2; ++rack) {
        const auto code = "rack0" + std::to_string(rack);
        const auto channel = "ch0" + std::to_string(rack);
        manifest["devices"].push_back({{"code", code},
                                       {"name", code},
                                       {"node_kind", "component"},
                                       {"parent_code", "hvac01"},
                                       {"source_device_code", "hvac01"},
                                       {"device_type_code", "hvac"},
                                       {"type_version", "hvac-v1"},
                                       {"enabled", false}});
        manifest["channels"].push_back({{"code", channel},
                                        {"name", channel},
                                        {"protocol", "modbus"},
                                        {"conn_mode", "tcp_client"},
                                        {"endpoint", "192.0.2.1:502"},
                                        {"enabled", 0}});
        manifest["bindings"].push_back({{"device_code", "hvac01"},
                                        {"channel_code", channel},
                                        {"device_address", std::to_string(rack + 1)},
                                        {"can_collect", true},
                                        {"enabled", false}});
        manifest["point_files"].push_back({{"device_code", "hvac01"},
                                           {"binding_code", channel},
                                           {"target_device_code", code},
                                           {"file", "rack.xlsx"}});
    }
    Upload upload;
    upload.filename = "station.zip";
    upload.bytes = station_zip(manifest, "rack.xlsx", bytes);
    auto inspection =
        service->execute("/api/station-imports/inspect", Json::object(), actor, context(), upload);
    EXPECT_EQ(inspection.data.at("station_code_match"), true);
    EXPECT_TRUE(inspection.data.at("warnings").empty());
    manifest["station"]["code"] = "another-station";
    upload.bytes = station_zip(manifest, "rack.xlsx", bytes);
    inspection =
        service->execute("/api/station-imports/inspect", Json::object(), actor, context(), upload);
    EXPECT_EQ(inspection.data.at("station_code_match"), false);
    EXPECT_EQ(inspection.data.at("station_code"), "station");
    EXPECT_EQ(inspection.data.at("package_station_code"), "another-station");
    EXPECT_EQ(inspection.meta.at("config_revision"), "0");
    EXPECT_TRUE(
        execute("/api/devices/tree", {{"parent_device_id", "station"}}).data.at("items").empty());
    auto conflicting = manifest;
    conflicting["point_files"][1]["target_device_code"] = "rack01";
    Upload invalid = upload;
    invalid.bytes = station_zip(conflicting, "rack.xlsx", bytes);
    // 第二个绑定在同一部件生成重复点编码，最终校验失败时连第一个绑定的点也不保留。
    EXPECT_THROW(
        service->execute("/api/station-imports/create", Json::object(), actor, context(), invalid),
        ApiError);
    EXPECT_TRUE(
        execute("/api/devices/tree", {{"parent_device_id", "station"}}).data.at("items").empty());
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        "0");
    auto result =
        service->execute("/api/station-imports/create", Json::object(), actor, context(), upload);
    EXPECT_EQ(result.data.at("station_code"), "station");
    EXPECT_EQ(result.data.at("package_station_code"), "another-station");
    EXPECT_EQ(result.data.at("warnings").front().at("code"), "station_code_mismatch");
    EXPECT_EQ(execute("/api/devices/detail", {{"device_id", "station"}}).data.at("code"),
              "station");
    ASSERT_EQ(result.data.at("import_jobs").size(), 2);
    std::string revision = result.meta.at("config_revision");
    for (const auto& job : result.data.at("import_jobs")) {
        ASSERT_EQ(job.at("status"), "applied") << job;
        auto points = execute("/api/points/list",
                              {{"device_id", job.at("target_device_id")}, {"point_type", "yc"}});
        EXPECT_EQ(points.data.at("total"), 1);
        auto commands = execute("/api/points/list",
                                {{"device_id", job.at("target_device_id")}, {"point_type", "yt"}});
        EXPECT_EQ(commands.data.at("total"), 1);
        EXPECT_EQ(commands.data.at("items").front().at("enabled"), false);
        auto preview =
            execute("/api/point-imports/rows", {{"import_job_id", job.at("import_job_id")}});
        auto row = preview.data.at("items").front();
        EXPECT_EQ(row.at("target_device_path"),
                  "/hvac01/" + job.at("target_device_code").get<std::string>());
        EXPECT_EQ(row.at("confirm_state"), "accepted");
        EXPECT_EQ(row.at("parse_status"), "warning");
    }
    EXPECT_EQ(result.data.at("points_created"), 4);
    EXPECT_EQ(revision, "1");
    auto replay =
        service->execute("/api/station-imports/create", Json::object(), actor, context(), upload);
    EXPECT_EQ(replay.data.at("points_created"), 0);
    EXPECT_EQ(replay.meta.at("config_revision"), revision);
    // 模拟旧流程留下的设备和待应用任务，重新上传原包即可补齐点位。
    sql("DELETE FROM cfg_point_yc");
    sql("DELETE FROM cfg_point_yt");
    sql("UPDATE cfg_import_job SET status='confirmed'");
    sql("UPDATE cfg_import_row SET resolved_point_id='',confirm_state='none'");
    auto repaired =
        service->execute("/api/station-imports/create", Json::object(), actor, context(), upload);
    EXPECT_EQ(repaired.data.at("points_created"), 4);
    revision = repaired.meta.at("config_revision");
    EXPECT_EQ(revision, "2");
    manifest["point_files"][0]["target_device_code"] = "missing";
    upload.bytes = station_zip(manifest, "rack.xlsx", bytes);
    EXPECT_THROW(
        service->execute("/api/station-imports/create", Json::object(), actor, context(), upload),
        ApiError);
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        revision);

    auto cleared = execute("/api/device-management/clear", {{"device_id", "station"},
                                                            {"expected_config_revision", revision},
                                                            {"confirmation_code", "station"},
                                                            {"station_code", "another-station"},
                                                            {"station_name", "New Station"}});
    EXPECT_EQ(cleared.meta.at("config_revision"), "3");
    manifest["point_files"][0]["target_device_code"] = "rack01";
    upload.bytes = station_zip(manifest, "rack.xlsx", bytes);
    auto reimported =
        service->execute("/api/station-imports/create", Json::object(), actor, context(), upload);
    EXPECT_TRUE(reimported.data.at("station_code_match").get<bool>());
    EXPECT_EQ(reimported.data.at("devices_created"), 3);
    EXPECT_EQ(reimported.data.at("points_created"), 4);
    EXPECT_EQ(reimported.meta.at("config_revision"), "4");
}

TEST_F(WebDevice, FieldStationPackageCreatesFourPcsFourBmsAndFortyEightRacks) {
    const auto* location = std::getenv("EMS_STATION_IMPORT_TEST_PACKAGE");
    if (!location)
        GTEST_SKIP() << "Set EMS_STATION_IMPORT_TEST_PACKAGE to verify a prepared field package";
    sql("UPDATE cfg_device SET code='station001' WHERE device_id='station'");
    for (const auto& type : {"pcs", "bms", "meter", "relay", "rack"}) {
        sql(std::string("INSERT INTO "
                        "cfg_device_type(type_code,type_version,name,category,status,description,"
                        "created_at,updated_at) VALUES('") +
            type + "','" + type + "-v1','" + type + "','" + type + "','active','','','')");
    }
    std::ifstream input(location, std::ios::binary);
    ASSERT_TRUE(input.good());
    Upload upload;
    upload.filename = "field_station.zip";
    upload.bytes.assign(std::istreambuf_iterator<char>(input), {});
    TestConnection::operation_count = 0;
    const auto started = std::chrono::steady_clock::now();
    const auto result = service->execute("/api/station-imports/create", Json::object(), actor,
                                         core::make_call_context(std::chrono::seconds(30)), upload);
    std::cout << "Station create: " << TestConnection::operation_count.load()
              << " database operations, "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - started)
                     .count()
              << " ms\n";
    EXPECT_LT(TestConnection::operation_count.load(), 15000U);
    EXPECT_EQ(result.data.at("devices_created"), 64);
    EXPECT_EQ(result.data.at("import_jobs").size(), 59);
    EXPECT_EQ(result.data.at("points_created"), 15617);
    EXPECT_EQ(result.meta.at("config_revision"), "1");
    {
        auto tx = take(db.database->begin(context()));
        DeviceQuery query;
        query.table = DeviceTable::ImportRow;
        query.limit = 1;
        EXPECT_EQ(take(db.device_management->query(*tx, query, context())).total, 15617);
        std::int64_t points = 0;
        for (auto kind : {DeviceTable::Yc, DeviceTable::Yx, DeviceTable::Yk, DeviceTable::Yt}) {
            query.table = kind;
            points += take(db.device_management->query(*tx, query, context())).total;
        }
        EXPECT_EQ(points, 15617);
        take(tx->commit(context()));
    }
    int total = 0;
    for (const auto& job : result.data.at("import_jobs")) {
        EXPECT_EQ(job.at("status"), "applied")
            << job << " "
            << execute("/api/point-imports/detail", {{"import_job_id", job.at("import_job_id")}})
                   .data.value("error_message", "");
        EXPECT_EQ(job.at("row_error"), 0) << job;
        total += job.at("row_total").get<int>();
    }
    EXPECT_EQ(total, 15617);
}

TEST_F(WebDevice, RuntimeSamplesAreAuthorizedFreshAndNeverFabricated) {
    execute("/api/devices/create", create());
    execute("/api/bindings/create", {{"device_id", "hvac01"},
                                     {"binding_id", "binding01"},
                                     {"expected_config_revision", "1"},
                                     {"fields",
                                      {{"new_channel",
                                        {{"channel_id", "channel01"},
                                         {"code", "channel01"},
                                         {"name", "Test"},
                                         {"protocol", "modbus"},
                                         {"conn_mode", "tcp_client"},
                                         {"endpoint", "127.0.0.1:502"},
                                         {"enabled", true}}},
                                       {"device_address", "1"},
                                       {"can_collect", true},
                                       {"enabled", true}}}});
    execute(
        "/api/points/create",
        {{"device_id", "hvac01"},
         {"point_type", "yc"},
         {"point_id", "temperature"},
         {"expected_config_revision", "2"},
         {"fields",
          {{"binding_id", "binding01"},
           {"code", "temperature"},
           {"name", "Temperature"},
           {"wire_type", "i16"},
           {"value_type", "double"},
           {"enabled", true},
           {"address", {{"space", "holding_register"}, {"offset", 0}, {"register_count", 1}}}}}});
    const Json body = {{"device_id", "hvac01"}, {"point_type", "yc"}};
    execute("/api/devices/update", {{"device_id", "hvac01"},
                                    {"expected_config_revision", "3"},
                                    {"fields", {{"enabled", true}}}});
    int reads = 0;
    RuntimeService fresh(db, {}, [&](const auto& kind, const auto& id, const auto&) {
        ++reads;
        EXPECT_EQ(kind, "yc");
        EXPECT_EQ(id, "temperature");
        return core::Result<std::string>{Json({{"code", id},
                                               {"value", 12.5},
                                               {"quality", 0},
                                               {"timestamp", core::now_utc_micros() / 1000}})
                                             .dump()};
    });
    auto result = fresh.execute("/api/monitor/points", body, actor, context());
    ASSERT_EQ(result.data.at("items").size(), 1);
    EXPECT_EQ(result.data.at("items").front().at("sample").at("value"), 12.5);
    EXPECT_EQ(result.data.at("items").front().at("quality"), "good");
    Principal denied{"reader", {"monitor.read"}, {"station"}, {"other"}};
    EXPECT_THROW(fresh.execute("/api/monitor/points", body, denied, context()), ApiError);
    EXPECT_EQ(reads, 1);
    RuntimeService missing(db, {}, [](const auto&, const auto&, const auto&) {
        return core::Result<std::string>{
            core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable)};
    });
    result = missing.execute("/api/monitor/points", body, actor, context());
    EXPECT_TRUE(result.data.at("items").front().at("sample").is_null());
    EXPECT_EQ(result.data.at("items").front().at("quality"), "missing");
    RuntimeService stale(db, {}, [](const auto&, const auto& id, const auto&) {
        return core::Result<std::string>{
            Json({{"code", id}, {"value", 1}, {"quality", 0}, {"timestamp", 1}}).dump()};
    });
    result = stale.execute("/api/monitor/points", body, actor, context());
    EXPECT_EQ(result.data.at("items").front().at("quality"), "stale");
    RuntimeService unavailable(db, {});
    EXPECT_THROW(unavailable.execute("/api/monitor/points", body, actor, context()), ApiError);
}

TEST_F(WebDevice, StationImportRoutesAreExplicitlyRegistered) {
    const auto routes = device_routes();
    EXPECT_NE(std::find_if(routes.begin(), routes.end(),
                           [](const auto& route) {
                               return route.path == "/api/device-management/clear" && route.post;
                           }),
              routes.end());
    EXPECT_NE(std::find_if(routes.begin(), routes.end(),
                           [](const auto& route) {
                               return route.path == "/api/station-imports/create" && route.post;
                           }),
              routes.end());
    EXPECT_EQ(execute("/api/station-imports/template", Json::object()).data.at("schema"),
              "ems-station-package/1");
}

TEST_F(WebDevice, HistoryReadsUnitScopeInBoundedPagesAndChecksOwnership) {
    execute("/api/devices/create", {{"device_id", "unit"},
                                    {"expected_config_revision", "0"},
                                    {"fields",
                                     {{"parent_device_id", "station"},
                                      {"node_kind", "unit"},
                                      {"code", "unit"},
                                      {"name", "Unit"},
                                      {"enabled", false}}}});
    auto device = create("hvac01", "1");
    device["fields"]["parent_device_id"] = "unit";
    execute("/api/devices/create", device);
    execute("/api/bindings/create", {{"device_id", "hvac01"},
                                     {"binding_id", "binding"},
                                     {"expected_config_revision", "2"},
                                     {"fields",
                                      {{"new_channel",
                                        {{"channel_id", "channel"},
                                         {"code", "channel"},
                                         {"name", "Test"},
                                         {"protocol", "modbus"},
                                         {"conn_mode", "tcp_client"},
                                         {"endpoint", "127.0.0.1:502"},
                                         {"enabled", false}}},
                                       {"device_address", "1"},
                                       {"can_collect", true},
                                       {"enabled", false}}}});
    execute(
        "/api/points/create",
        {{"device_id", "hvac01"},
         {"point_type", "yc"},
         {"point_id", "temperature"},
         {"expected_config_revision", "3"},
         {"fields",
          {{"binding_id", "binding"},
           {"code", "temperature"},
           {"name", "Temperature"},
           {"wire_type", "i16"},
           {"value_type", "double"},
           {"enabled", false},
           {"disabled_reason", "Test"},
           {"address", {{"space", "holding_register"}, {"offset", 0}, {"register_count", 1}}}}}});

    class Catalog final : public IHistoryCatalogRepository {
       public:
        core::Result<PointSnapshot> find_point(ITransaction&, const std::string&,
                                               const std::string&, const std::string&,
                                               const std::string&,
                                               const core::CallContext&) override {
            return core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable);
        }

        core::Result<SeriesMapping> find_series(ITransaction&, const std::string& id,
                                                const core::CallContext&) override {
            return SeriesMapping{id, ValueType::Numeric, "yc"};
        }
    };

    db.history_catalog = std::make_shared<Catalog>();
    int reads = 0;
    RuntimeService history(
        db, {}, {},
        [&](const HistoryQuery& query, const std::vector<SeriesMapping>& mappings,
            std::uint32_t limit, const auto&) {
            ++reads;
            EXPECT_LE(limit, 1001u);
            EXPECT_EQ(mappings.size(), 1u);
            std::vector<Sample> samples;
            if (query.scope_kind == "unit") {
                EXPECT_EQ(query.scope_id, "unit");
                const auto start = query.cursor ? query.cursor->source_time_us / 1000 + 1 : 1;
                for (auto i = start; i <= 1500 && samples.size() < limit; ++i) {
                    Sample sample;
                    sample.scope_kind = "unit";
                    sample.scope_id = "unit";
                    sample.point_id = "temperature";
                    sample.series_key = "yc";
                    sample.event_id = std::to_string(i);
                    sample.source_time_us = i * 1000;
                    sample.value = static_cast<double>(i);
                    samples.push_back(sample);
                }
            }
            return core::Result<std::vector<Sample>>{samples};
        });
    Json body = {{"device_id", "hvac01"},
                 {"point_ids", {"temperature"}},
                 {"start_ms", 0},
                 {"end_ms", 2000},
                 {"max_rows", 1200}};
    auto result = history.execute("/api/history/query", body, actor, context());
    EXPECT_EQ(reads, 3);
    ASSERT_EQ(result.data.at("items").size(), 1200u);
    EXPECT_EQ(result.data.at("items").back().at("timestamp"), 1200);
    EXPECT_TRUE(result.data.at("truncated"));
    body["max_rows"] = 2000;
    result = history.execute("/api/history/query", body, actor, context());
    EXPECT_EQ(result.data.at("items").size(), 1500u);
    EXPECT_FALSE(result.data.at("truncated"));
    Principal denied{"reader", {"history.read"}, {"station"}, {"other"}};
    const auto before = reads;
    EXPECT_THROW(history.execute("/api/history/query", body, denied, context()), ApiError);
    EXPECT_EQ(reads, before);
    RuntimeService corrupt(db, {}, {}, [](const auto&, const auto&, auto, const auto&) {
        Sample sample;
        sample.scope_kind = "station";
        sample.scope_id = "other-station";
        return core::Result<std::vector<Sample>>{std::vector<Sample>{sample}};
    });
    EXPECT_THROW(corrupt.execute("/api/history/query", body, actor, context()), ApiError);
}

TEST_F(WebDevice, CreateQueryRevisionAndNoOpSave) {
    auto saved = execute("/api/devices/create", create());
    EXPECT_EQ(saved.status, 201);
    EXPECT_EQ(saved.meta.at("config_revision"), "1");
    auto replay = execute("/api/devices/create", create());
    EXPECT_TRUE(replay.meta.at("replayed").get<bool>());
    EXPECT_EQ(replay.meta.at("config_revision"), "1");
    auto detail = execute("/api/devices/detail", {{"device_id", "hvac01"}});
    EXPECT_EQ(detail.data.at("path"), "/hvac01");
    EXPECT_FALSE(detail.data.at("enabled").get<bool>());
    auto no_op = execute("/api/devices/update", {{"device_id", "hvac01"},
                                                 {"expected_config_revision", "1"},
                                                 {"fields", {{"name", "HVAC"}}}});
    EXPECT_EQ(no_op.meta.at("config_revision"), "1");
}

TEST_F(WebDevice, StaleWriteAndInvalidCandidateLeaveRevisionUnchanged) {
    execute("/api/devices/create", create());
    try {
        execute("/api/devices/update", {{"device_id", "hvac01"},
                                        {"expected_config_revision", "0"},
                                        {"fields", {{"name", "lost update"}}}});
        FAIL();
    } catch (const ApiError& error) {
        EXPECT_EQ(error.status, 409);
    }
    auto invalid = create("hvac02", "1");
    invalid["fields"]["device_type_code"] = "missing";
    EXPECT_THROW(execute("/api/devices/create", invalid), ApiError);
    auto revision = execute("/api/device-management/configuration", Json::object());
    EXPECT_EQ(revision.meta.at("config_revision"), "1");
    EXPECT_THROW(execute("/api/devices/detail", {{"device_id", "hvac02"}}), ApiError);
}

TEST_F(WebDevice, ScopeFilteringDoesNotLeakTotalOrDetails) {
    execute("/api/devices/create", create());
    execute("/api/devices/create", create("hvac02", "1"));
    actor.device_roots = {"hvac01"};
    actor.permissions = {"device.read"};
    auto tree = execute("/api/devices/tree", Json::object());
    EXPECT_EQ(tree.data.at("total"), 1);
    auto children = execute("/api/devices/tree", {{"parent_device_id", "station"}});
    EXPECT_EQ(children.data.at("total"), 1);
    auto page = execute("/api/devices/list", {{"page_size", 1}});
    EXPECT_EQ(page.data.at("total"), 1);
    EXPECT_EQ(page.data.at("items").size(), 1u);
    EXPECT_THROW(execute("/api/devices/detail", {{"device_id", "hvac02"}}), ApiError);
    EXPECT_THROW(execute("/api/devices/delete",
                         {{"device_id", "hvac01"}, {"expected_config_revision", "2"}}),
                 ApiError);
}

TEST_F(WebDevice, SqlFiltersAreBoundAndSortIsWhitelisted) {
    execute("/api/devices/create", create());
    auto page = execute("/api/devices/list", {{"filters", {{"keyword", "' OR 1=1 --"}}}});
    EXPECT_EQ(page.data.at("total"), 0);
    EXPECT_THROW(execute("/api/devices/list", {{"sort", "name; DELETE FROM cfg_device"}}),
                 ApiError);
}

TEST_F(WebDevice, ValidationDoesNotCommitCandidateChanges) {
    execute("/api/devices/create", create());
    auto result = execute(
        "/api/device-management/validate",
        {{"device_id", "hvac01"}, {"expected_config_revision", "1"}, {"changes", Json::array()}});
    EXPECT_TRUE(result.data.at("valid").get<bool>());
    EXPECT_EQ(result.meta.at("config_revision"), "1");
}

TEST_F(WebDevice, StationClearDeletesDependenciesAndRejectsUnconfirmedStaleOrScopedRequests) {
    bind_protocol("modbus");
    Json clear = {{"device_id", "station"},
                  {"expected_config_revision", "2"},
                  {"confirmation_code", "station"},
                  {"station_code", "new-station"},
                  {"station_name", "New Station"}};
    auto invalid = clear;
    invalid["confirmation_code"] = "wrong";
    EXPECT_THROW(execute("/api/device-management/clear", invalid), ApiError);
    invalid = clear;
    invalid["expected_config_revision"] = "1";
    EXPECT_THROW(execute("/api/device-management/clear", invalid), ApiError);
    invalid = clear;
    invalid["station_code"] = "invalid/code";
    EXPECT_THROW(execute("/api/device-management/clear", invalid), ApiError);
    invalid = clear;
    invalid["device_id"] = "hvac01";
    EXPECT_THROW(execute("/api/device-management/clear", invalid), ApiError);
    actor.device_roots = {"station"};
    EXPECT_THROW(execute("/api/device-management/clear", clear), ApiError);
    actor.device_roots = {"*"};
    actor.permissions = {"device.read", "device.write"};
    EXPECT_THROW(execute("/api/device-management/clear", clear), ApiError);
    actor.permissions = {"*"};
    EXPECT_EQ(execute("/api/devices/detail", {{"device_id", "hvac01"}}).data.at("code"), "hvac01");
    int next_revision = 2;
    for (auto kind : {"yc", "yx", "yk", "yt"}) {
        execute("/api/points/create",
                {{"device_id", "hvac01"},
                 {"point_type", kind},
                 {"point_id", kind},
                 {"expected_config_revision", std::to_string(next_revision++)},
                 {"fields",
                  {{"binding_id", "binding01"},
                   {"code", kind},
                   {"name", kind},
                   {"wire_type", "i16"},
                   {"value_type",
                    kind == std::string("yc") || kind == std::string("yt") ? "double" : "int64"},
                   {"enabled", false},
                   {"disabled_reason", "not commissioned"}}}});
    }
    sql("INSERT INTO cfg_import_job(import_job_id,device_id,binding_id,idempotency_key) "
        "VALUES('job','hvac01','binding01','key'); "
        "INSERT INTO cfg_import_row(import_job_id,line_no) VALUES('job',1); "
        "INSERT INTO ops_comm_stat_5m(binding_id) VALUES('binding01'); "
        "INSERT INTO ops_state_log(object_kind,object_id) VALUES('device','hvac01'); "
        "INSERT INTO cfg_template(template_id,template_version) VALUES('template','1'); "
        "INSERT INTO cfg_type_point(type_code,type_version,semantic_code) "
        "VALUES('hvac','hvac-v1','temperature')");
    clear["expected_config_revision"] = "6";
    // 模拟删除中途失败，先删的点位、导入记录和统计也必须恢复。
    sql("CREATE TRIGGER reject_clear BEFORE DELETE ON cfg_binding BEGIN SELECT "
        "RAISE(ABORT,'test'); END");
    EXPECT_THROW(execute("/api/device-management/clear", clear), ApiError);
    auto count = [&](DeviceTable table) {
        auto tx = take(db.database->begin(context()));
        DeviceQuery q;
        q.table = table;
        q.limit = 1;
        return take(db.device_management->query(*tx, q, context())).total;
    };
    for (auto table : {DeviceTable::Yc, DeviceTable::Yx, DeviceTable::Yk, DeviceTable::Yt,
                       DeviceTable::ImportRow, DeviceTable::ImportJob, DeviceTable::CommStat,
                       DeviceTable::StateLog, DeviceTable::Binding, DeviceTable::Channel})
        EXPECT_EQ(count(table), 1);
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        "6");
    sql("DROP TRIGGER reject_clear");
    const auto result = execute("/api/device-management/clear", clear);
    EXPECT_EQ(result.meta.at("config_revision"), "7");
    for (auto table : {DeviceTable::Yc, DeviceTable::Yx, DeviceTable::Yk, DeviceTable::Yt,
                       DeviceTable::ImportRow, DeviceTable::ImportJob, DeviceTable::CommStat,
                       DeviceTable::StateLog, DeviceTable::Binding, DeviceTable::Channel})
        EXPECT_EQ(count(table), 0);
    EXPECT_EQ(count(DeviceTable::Device), 1);
    EXPECT_EQ(count(DeviceTable::Type), 1);
    EXPECT_EQ(count(DeviceTable::TypePoint), 1);
    EXPECT_EQ(count(DeviceTable::Template), 1);
    const auto station = execute("/api/devices/detail", {{"device_id", "station"}}).data;
    EXPECT_EQ(station.at("name"), "New Station");
    EXPECT_EQ(station.at("code"), "new-station");
    EXPECT_EQ(station.at("path"), "/");
    EXPECT_THROW(execute("/api/device-management/clear", clear), ApiError);
}

TEST_F(WebDevice, MissingRequiredPointsWarnWithoutBlockingEnableOrSubsequentWrites) {
    sql("INSERT INTO cfg_type_point(type_code,type_version,semantic_code,name,point_type,"
        "value_type,unit,required,capability_codes_json,writable,stale_after_ms,sort_order) "
        "VALUES('hvac','hvac-v1','temperature','Temperature','yc','double','C',1,'[]',0,0,0)");
    execute("/api/devices/create", create());
    auto enabled = execute("/api/devices/update", {{"device_id", "hvac01"},
                                                   {"expected_config_revision", "1"},
                                                   {"fields", {{"enabled", true}}}});
    EXPECT_EQ(enabled.meta.at("config_revision"), "2");
    ASSERT_EQ(enabled.meta.at("warnings").size(), 1u);
    const auto& warning = enabled.meta.at("warnings").front();
    EXPECT_EQ(warning.at("severity"), "warning");
    EXPECT_EQ(warning.at("reason"), "required_point_unconfigured");
    EXPECT_EQ(warning.at("device_id"), "hvac01");
    EXPECT_NE(warning.at("message").get<std::string>().find("temperature"), std::string::npos);
    auto detail = execute("/api/devices/detail", {{"device_id", "hvac01"}});
    EXPECT_TRUE(detail.data.at("enabled").get<bool>());
    auto check = execute("/api/device-management/validate",
                         {{"device_id", "hvac01"}, {"expected_config_revision", "2"}});
    EXPECT_FALSE(check.data.at("valid").get<bool>());
    EXPECT_EQ(check.data.at("counts").at("errors"), 0);
    EXPECT_EQ(check.data.at("counts").at("warnings"), 1);
    EXPECT_EQ(check.meta.at("config_revision"), "2");
    auto saved = execute("/api/devices/update", {{"device_id", "hvac01"},
                                                 {"expected_config_revision", "2"},
                                                 {"fields", {{"name", "Updated HVAC"}}}});
    EXPECT_EQ(saved.meta.at("config_revision"), "3");
    EXPECT_EQ(saved.meta.at("warnings").size(), 1u);

    auto invalid = create("invalid", "3");
    invalid["fields"]["device_type_code"] = "missing";
    EXPECT_THROW(execute("/api/devices/create", invalid), ApiError);
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        "3");
    auto disabled = execute("/api/devices/update", {{"device_id", "hvac01"},
                                                    {"expected_config_revision", "3"},
                                                    {"fields", {{"enabled", false}}}});
    EXPECT_FALSE(disabled.meta.contains("warnings"));
}

TEST_F(WebDevice, BindingFourRemotePointsAndAtomicRollback) {
    execute("/api/devices/create", create());
    auto binding = execute("/api/bindings/create", {{"device_id", "hvac01"},
                                                    {"binding_id", "binding01"},
                                                    {"expected_config_revision", "1"},
                                                    {"fields",
                                                     {{"new_channel",
                                                       {{"channel_id", "channel01"},
                                                        {"code", "channel01"},
                                                        {"name", "shared"},
                                                        {"protocol", "modbus"},
                                                        {"conn_mode", "tcp_client"},
                                                        {"endpoint", "127.0.0.1:502"},
                                                        {"enabled", true}}},
                                                      {"device_address", "1"},
                                                      {"can_collect", true},
                                                      {"can_control", true},
                                                      {"enabled", true}}}});
    EXPECT_EQ(binding.meta.at("config_revision"), "2");
    Json point = {
        {"device_id", "hvac01"},
        {"point_type", "yc"},
        {"point_id", "temperature"},
        {"expected_config_revision", "2"},
        {"fields",
         {{"binding_id", "binding01"},
          {"code", "temperature"},
          {"name", "Temperature"},
          {"address", {{"space", "holding_register"}, {"offset", 0}, {"register_count", 1}}},
          {"wire_type", "i16"},
          {"value_type", "double"},
          {"scale_factor", 0.1},
          {"enabled", true}}}};
    auto saved = execute("/api/points/create", point);
    EXPECT_EQ(saved.meta.at("config_revision"), "3");
    auto replay = execute("/api/points/create", point);
    EXPECT_TRUE(replay.meta.at("replayed").get<bool>());
    EXPECT_EQ(replay.meta.at("config_revision"), "3");
    auto details =
        execute("/api/points/detail",
                {{"device_id", "hvac01"}, {"point_type", "yc"}, {"point_id", "temperature"}});
    EXPECT_EQ(details.data.at("address").at("offset"), 0);
    auto invalid = point;
    invalid["point_id"] = "bad";
    invalid["expected_config_revision"] = "3";
    invalid["fields"]["code"] = "bad";
    invalid["fields"]["scale_factor"] = 0;
    EXPECT_THROW(execute("/api/points/create", invalid), ApiError);
    EXPECT_THROW(execute("/api/points/detail",
                         {{"device_id", "hvac01"}, {"point_type", "yc"}, {"point_id", "bad"}}),
                 ApiError);
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        "3");
    EXPECT_THROW(execute("/api/bindings/delete", {{"device_id", "hvac01"},
                                                  {"binding_id", "binding01"},
                                                  {"expected_config_revision", "3"}}),
                 ApiError);
}

TEST_F(WebDevice, ChannelAndBindingReplayRejectDifferentContent) {
    execute("/api/devices/create", create());
    Json connection = {{"channel_id", "channel01"},
                       {"expected_config_revision", "1"},
                       {"fields",
                        {{"code", "channel01"},
                         {"name", "shared"},
                         {"protocol", "modbus"},
                         {"conn_mode", "tcp_client"},
                         {"endpoint", "127.0.0.1:502"}}}};
    EXPECT_EQ(execute("/api/channels/create", connection).status, 201);
    auto replay = execute("/api/channels/create", connection);
    EXPECT_TRUE(replay.meta.at("replayed").get<bool>());
    EXPECT_EQ(replay.meta.at("config_revision"), "2");

    Json binding = {
        {"device_id", "hvac01"},
        {"binding_id", "binding01"},
        {"expected_config_revision", "2"},
        {"fields", {{"channel_id", "channel01"}, {"device_address", "1"}, {"can_collect", true}}}};
    EXPECT_EQ(execute("/api/bindings/create", binding).status, 201);
    replay = execute("/api/bindings/create", binding);
    EXPECT_TRUE(replay.meta.at("replayed").get<bool>());
    EXPECT_EQ(replay.meta.at("config_revision"), "3");

    // 相同稳定 ID 只有内容一致时可重放，不能覆盖后来保存的配置。
    binding["fields"]["device_address"] = "2";
    EXPECT_THROW(execute("/api/bindings/create", binding), ApiError);
    connection["fields"]["endpoint"] = "127.0.0.1:503";
    EXPECT_THROW(execute("/api/channels/create", connection), ApiError);
    EXPECT_EQ(
        execute("/api/device-management/configuration", Json::object()).meta.at("config_revision"),
        "3");
}

TEST_F(WebDevice, ReplayComparesJsonContentAcrossDatabaseSerialization) {
    Json saved = {{"channel_id", "serial"},
                  {"serial_json", "{\"parity\": \"none\", \"baud_rate\": 9600}"}};
    Json candidate = {{"channel_id", "serial"},
                      {"serial_json", "{\"baud_rate\":9600,\"parity\":\"none\"}"}};
    EXPECT_TRUE(same_record(saved, candidate));
    candidate["serial_json"] = "{\"baud_rate\":19200,\"parity\":\"none\"}";
    EXPECT_FALSE(same_record(saved, candidate));
}

TEST_F(WebDevice, BatchedImportRowsRollBackEarlierBatchesOnDuplicate) {
    std::vector<CfgImportRow> rows(130);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].import_job_id = "batch-job";
        rows[i].line_no = static_cast<int>(i + 1);
        rows[i].vendor_key = "point-" + std::to_string(i);
    }
    rows.back().line_no = 1;
    {
        auto tx = take(db.database->begin(context()));
        auto status = db.device_management->append_import_rows(*tx, rows, context());
        ASSERT_FALSE(core::is_ok(status));
        EXPECT_FALSE(core::is_ok(tx->commit(context())));
    }
    DeviceQuery query;
    query.table = DeviceTable::ImportRow;
    query.filters = {{"import_job_id", "batch-job"}};
    {
        auto tx = take(db.database->begin(context()));
        EXPECT_EQ(take(db.device_management->query(*tx, query, context())).total, 0);
        take(tx->commit(context()));
    }
    rows.back().line_no = 130;
    {
        auto tx = take(db.database->begin(context()));
        take(db.device_management->append_import_rows(*tx, rows, context()));
        take(tx->commit(context()));
    }
    auto tx = take(db.database->begin(context()));
    const auto saved = take(db.device_management->query(*tx, query, context()));
    EXPECT_EQ(saved.total, 130);
    EXPECT_EQ(std::get<CfgImportRow>(saved.rows.back()).vendor_key, "point-129");
    take(tx->commit(context()));
}

TEST_F(WebDevice, CsvPreviewApplyStableIdentityAndReplay) {
    execute("/api/devices/create", create());
    execute("/api/bindings/create", {{"device_id", "hvac01"},
                                     {"binding_id", "binding01"},
                                     {"expected_config_revision", "1"},
                                     {"fields",
                                      {{"new_channel",
                                        {{"channel_id", "channel01"},
                                         {"code", "channel01"},
                                         {"name", "shared"},
                                         {"protocol", "modbus"},
                                         {"conn_mode", "tcp_client"},
                                         {"endpoint", "127.0.0.1:502"},
                                         {"enabled", true}}},
                                       {"device_address", "1"},
                                       {"can_collect", true},
                                       {"enabled", true}}}});
    Upload upload;
    upload.filename = "points.csv";
    upload.bytes =
        "template_key,standard_code,vendor_name,address_space,address_offset,access,wire_type,byte_"
        "order,scale,offset,unit\nreg0,,Temperature,holding_register,0,read,i16,,0.1,0,C\n"
        "reg1,,Voltage,holding_register,1,read,i16,,1,0,V\n"
        "reg40001,,Power,holding_register,40001,read,i16,,1,0,W\n"
        "reg65535,,Current,holding_register,65535,read,i16,,1,0,A\n";
    Json metadata = {
        {"device_id", "hvac01"}, {"binding_id", "binding01"}, {"source_kind", "vendor_csv"}};
    auto preview =
        service->execute("/api/point-imports/create", metadata, actor, context(), upload);
    EXPECT_EQ(preview.status, 201);
    EXPECT_EQ(preview.meta.at("config_revision"), "2");
    EXPECT_EQ(preview.data.at("status"), "confirmed") << preview.data;
    const auto id = preview.data.at("import_job_id");
    EXPECT_FALSE(preview.data.contains("address_base"));
    auto parsed_rows = execute("/api/point-imports/rows", {{"import_job_id", id}});
    ASSERT_EQ(parsed_rows.data.at("items").size(), 4);
    const std::vector<int> addresses = {0, 1, 40001, 65535};
    for (std::size_t i = 0; i < addresses.size(); ++i)
        EXPECT_EQ(parsed_rows.data.at("items").at(i).at("address").at("offset"), addresses[i]);
    auto replay = service->execute("/api/point-imports/create", metadata, actor, context(), upload);
    EXPECT_EQ(replay.data.at("import_job_id"), id);
    EXPECT_TRUE(replay.meta.at("replayed").get<bool>());
    Json apply = {{"import_job_id", id},
                  {"expected_config_revision", "2"},
                  {"expected_preview_hash", preview.data.at("preview_hash")},
                  {"mode", "merge"}};
    auto applied = execute("/api/point-imports/apply", apply);
    EXPECT_EQ(applied.data.at("status"), "applied");
    EXPECT_EQ(applied.meta.at("config_revision"), "3");
    auto repeated = execute("/api/point-imports/apply", apply);
    EXPECT_TRUE(repeated.meta.at("replayed").get<bool>());
    EXPECT_EQ(repeated.meta.at("config_revision"), "3");
    auto rows = execute("/api/point-imports/rows", {{"import_job_id", id}});
    EXPECT_FALSE(rows.data.at("items").front().at("resolved_point_id").get<std::string>().empty());
    auto list = execute("/api/points/list", {{"device_id", "hvac01"}, {"point_type", "yc"}});
    EXPECT_EQ(list.data.at("total"), 4);
    std::set<int> saved_addresses;
    for (const auto& point : list.data.at("items"))
        saved_addresses.insert(point.at("address").at("offset").get<int>());
    EXPECT_EQ(saved_addresses, (std::set<int>{0, 1, 40001, 65535}));
}

TEST_F(WebDevice, XlsxPreviewParsesRealWorkbookWithoutChangingRevision) {
    execute("/api/devices/create", create());
    execute("/api/bindings/create", {{"device_id", "hvac01"},
                                     {"binding_id", "binding01"},
                                     {"expected_config_revision", "1"},
                                     {"fields",
                                      {{"new_channel",
                                        {{"channel_id", "channel01"},
                                         {"code", "channel01"},
                                         {"name", "shared"},
                                         {"protocol", "modbus"},
                                         {"conn_mode", "tcp_client"},
                                         {"endpoint", "127.0.0.1:502"}}},
                                       {"device_address", "1"},
                                       {"can_collect", true}}}});
    const auto path = directory / std::filesystem::u8path("厂家点表.xlsx");
    util::Excel excel;
    excel.create(path);
    excel.renameSheet("Sheet1", "点表");
    excel.writeRange(
        "点表", 1, 1,
        {{std::string("template_key"), std::string("standard_code"), std::string("vendor_name"),
          std::string("address_space"), std::string("address_offset"), std::string("access"),
          std::string("wire_type"), std::string("byte_order"), std::string("scale"),
          std::string("offset"), std::string("unit")},
         {std::string("reg0"), std::string{}, std::string("温度"), std::string("holding_register"),
          std::int64_t{1}, std::string("read"), std::string("i16"), std::string{}, 0.1, 0.0,
          std::string("C")}});
    excel.save();
    excel.close();

    std::ifstream input(path, std::ios::binary);
    Upload upload;
    upload.filename = "厂家点表.xlsx";
    upload.bytes.assign(std::istreambuf_iterator<char>(input), {});
    input.close();
    auto preview = service->execute(
        "/api/point-imports/create",
        {{"device_id", "hvac01"}, {"binding_id", "binding01"}, {"source_kind", "vendor_xlsx"}},
        actor, context(), upload);
    EXPECT_EQ(preview.data.at("status"), "confirmed") << preview.data;
    EXPECT_EQ(preview.meta.at("config_revision"), "2");
    auto rows =
        execute("/api/point-imports/rows", {{"import_job_id", preview.data.at("import_job_id")}});
    ASSERT_EQ(rows.data.at("total"), 1);
    EXPECT_EQ(rows.data.at("items").front().at("vendor_name"), "温度");
    EXPECT_EQ(rows.data.at("items").front().at("address").at("offset"), 1);

    // 重复表头必须保留解析错误，不能生成没有行的成功预览。
    excel.open(path);
    excel.writeCell("点表", 1, 7, std::string("access"));
    excel.save();
    excel.close();
    std::ifstream invalid_input(path, std::ios::binary);
    upload.bytes.assign(std::istreambuf_iterator<char>(invalid_input), {});
    EXPECT_NE(sha256(upload.bytes), preview.data.at("file_sha256"));
    const auto invalid = service->execute(
        "/api/point-imports/create",
        {{"device_id", "hvac01"}, {"binding_id", "binding01"}, {"source_kind", "vendor_xlsx"}},
        actor, context(), upload);
    EXPECT_EQ(invalid.data.at("status"), "failed");
    EXPECT_EQ(invalid.meta.at("config_revision"), "2");
}

TEST_F(WebDevice, HttpLoginCookieCsrfAndFixedJsonRoutes) {
    std::string station_bytes;
    int expected_station_devices = 1, expected_station_jobs = 1;
    if (const auto* package_path = std::getenv("EMS_STATION_IMPORT_TEST_PACKAGE")) {
        std::ifstream package(package_path, std::ios::binary);
        ASSERT_TRUE(package.good());
        station_bytes.assign(std::istreambuf_iterator<char>(package), {});
        expected_station_devices = 64;
        expected_station_jobs = 59;
        sql("UPDATE cfg_device SET code='current-station' WHERE device_id='station'");
        for (const auto& type : {"pcs", "bms", "meter", "relay", "rack"})
            sql(std::string("INSERT INTO "
                            "cfg_device_type(type_code,type_version,name,category,status,"
                            "description,created_at,updated_at) VALUES('") +
                type + "','" + type + "-v1','" + type + "','" + type + "','active','','','')");
    } else {
        const auto path = directory / "station_points.xlsx";
        util::Excel excel;
        excel.create(path);
        excel.renameSheet("Sheet1", "遥测");
        excel.writeRange(
            "遥测", 1, 1,
            {{std::string("测点编码"), std::string("测点名称"), std::string("从机地址"),
              std::string("寄存器地址"), std::string("功能码"), std::string("解析码")},
             {std::string("voltage"), std::string("电压"), std::int64_t{1}, std::int64_t{0},
              std::int64_t{4}, std::string("UINT16_AB")}});
        excel.save();
        excel.close();
        std::ifstream input(path, std::ios::binary);
        std::string bytes(std::istreambuf_iterator<char>(input), {});
        Json manifest = {
            {"schema", "ems-station-package/1"},
            {"station", {{"code", "station"}}},
            {"devices", Json::array({{{"code", "station_http_device"},
                                      {"name", "Test"},
                                      {"node_kind", "device"},
                                      {"device_type_code", "hvac"},
                                      {"type_version", "hvac-v1"},
                                      {"enabled", false}}})},
            {"channels", Json::array({{{"code", "station_http_channel"},
                                       {"name", "Test"},
                                       {"protocol", "modbus"},
                                       {"conn_mode", "tcp_client"},
                                       {"endpoint", "192.0.2.1:502"},
                                       {"enabled", false}}})},
            {"bindings", Json::array({{{"device_code", "station_http_device"},
                                       {"channel_code", "station_http_channel"},
                                       {"device_address", "1"},
                                       {"can_collect", true},
                                       {"enabled", false}}})},
            {"point_files",
             Json::array({{{"device_code", "station_http_device"}, {"file", "points.xlsx"}}})}};
        station_bytes = station_zip(manifest, "points.xlsx", bytes);
    }
    WebOptions options;
    options.port = 19763;
    options.allowed_origin = "http://127.0.0.1:19763";
    options.workspace = (directory / "http-upload").u8string();
    auto identity = std::make_shared<SessionService>(
        Json::array({{{"username", "operator"},
                      {"password_hash", hash_password("http-test-password")},
                      {"permissions", Json::array({"*"})},
                      {"station_ids", Json::array({"*"})},
                      {"device_roots", Json::array({"*"})}}}),
        1000, 3600);

    // 服务在当前主线程运行；客户端放到独立线程，保证事件循环的创建和销毁线程一致。
    (void)drogon::app();
    std::thread client_thread([&] {
        struct QuitOnExit {
            ~QuitOnExit() { drogon::app().quit(); }
        } quit;
        try {
            for (int i = 0; i < 200 && !drogon::app().isRunning(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ASSERT_TRUE(drogon::app().isRunning());
            auto client = drogon::HttpClient::newHttpClient(options.allowed_origin);
            std::string cookie, csrf;
            auto call = [&](const std::string& path, drogon::HttpMethod method, const Json& body,
                            bool credentials = true) {
                auto request = drogon::HttpRequest::newHttpRequest();
                request->setPath(path);
                request->setMethod(method);
                request->addHeader("Origin", options.allowed_origin);
                if (method == drogon::Post) {
                    request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    request->setBody(body.dump());
                }
                if (credentials && !cookie.empty()) request->addHeader("Cookie", cookie);
                if (credentials && !csrf.empty()) request->addHeader("X-CSRF-Token", csrf);
                const auto result = client->sendRequest(request, 5);
                if (result.first != drogon::ReqResult::Ok)
                    throw std::runtime_error("HTTP transport failed");
                return result.second;
            };

            EXPECT_EQ(call("/health/ready", drogon::Get, {})->statusCode(), 200);
            EXPECT_EQ(call("/api/devices/list", drogon::Post, Json::object(), false)->statusCode(),
                      401);
            auto login =
                call("/api/auth/login", drogon::Post,
                     {{"username", "operator"}, {"password", "http-test-password"}}, false);
            ASSERT_EQ(login->statusCode(), 200) << login->body();
            // Cookie 与 CSRF 由真实 HTTP 入口建立，测试不绕过身份模块。
            auto header = login->getHeader("set-cookie");
            cookie = header.substr(0, header.find(';'));
            if (cookie.empty() && !login->cookies().empty()) {
                const auto& saved = login->cookies().begin()->second;
                cookie = saved.key() + "=" + saved.value();
            }
            csrf =
                Json::parse(login->body().begin(), login->body().end()).at("data").at("csrf_token");
            ASSERT_FALSE(cookie.empty());
            EXPECT_EQ(call("/api/devices/list", drogon::Post, Json::object())->statusCode(), 200);
            auto created = call("/api/devices/create", drogon::Post, create());
            EXPECT_EQ(created->statusCode(), 201) << created->body();
            auto details = call("/api/devices/detail", drogon::Post, {{"device_id", "hvac01"}});
            EXPECT_EQ(details->statusCode(), 200);
            EXPECT_EQ(Json::parse(details->body().begin(), details->body().end())
                          .at("data")
                          .at("device_id"),
                      "hvac01");
            auto wrong = call("/api/devices/detail", drogon::Get, {});
            EXPECT_EQ(wrong->statusCode(), 405);
            EXPECT_EQ(
                Json::parse(wrong->body().begin(), wrong->body().end()).at("error").at("reason"),
                "method_not_allowed");
            EXPECT_EQ(call("/api/devices/update", drogon::Post,
                           {{"device_id", "hvac01"},
                            {"expected_config_revision", "0"},
                            {"fields", {{"name", "stale"}}}})
                          ->statusCode(),
                      409);
            auto protected_request = drogon::HttpRequest::newHttpRequest();
            protected_request->setPath("/api/devices/list");
            protected_request->setMethod(drogon::Post);
            protected_request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            protected_request->setBody("{}");
            protected_request->addHeader("Origin", options.allowed_origin);
            protected_request->addHeader("Cookie", cookie);
            auto denied = client->sendRequest(protected_request, 5);
            ASSERT_EQ(denied.first, drogon::ReqResult::Ok);
            EXPECT_EQ(denied.second->statusCode(), 403);

            auto bound = call("/api/bindings/create", drogon::Post,
                              {{"device_id", "hvac01"},
                               {"binding_id", "http-binding"},
                               {"expected_config_revision", "1"},
                               {"fields",
                                {{"new_channel",
                                  {{"channel_id", "http-channel"},
                                   {"code", "http-channel"},
                                   {"name", "HTTP channel"},
                                   {"protocol", "modbus"},
                                   {"conn_mode", "tcp_client"},
                                   {"endpoint", "127.0.0.1:502"},
                                   {"enabled", true}}},
                                 {"device_address", "1"},
                                 {"can_collect", true},
                                 {"enabled", true}}}});
            ASSERT_EQ(bound->statusCode(), 201) << bound->body();
            const std::string boundary = "ems-http-test-boundary";
            const std::string csv =
                "template_key,standard_code,vendor_name,address_space,address_offset,access,wire_"
                "type,byte_order,scale,offset,unit\nreg0,,Temperature,holding_register,0,read,i16,,"
                "0.1,0,C\n";
            auto metadata = Json{{"device_id", "hvac01"},
                                 {"binding_id", "http-binding"},
                                 {"source_kind", "vendor_csv"}}
                                .dump();
            auto upload = drogon::HttpRequest::newHttpRequest();
            upload->setPath("/api/point-imports/create");
            upload->setMethod(drogon::Post);
            upload->setContentTypeString("multipart/form-data; boundary=" + boundary);
            upload->addHeader("Origin", options.allowed_origin);
            upload->addHeader("Cookie", cookie);
            upload->addHeader("X-CSRF-Token", csrf);
            upload->setBody("--" + boundary +
                            "\r\nContent-Disposition: form-data; "
                            "name=\"metadata\"\r\nContent-Type: application/json\r\n\r\n" +
                            metadata + "\r\n--" + boundary +
                            "\r\nContent-Disposition: form-data; name=\"file\"; "
                            "filename=\"points.csv\"\r\nContent-Type: text/csv\r\n\r\n" +
                            csv + "\r\n--" + boundary + "--\r\n");
            auto preview = client->sendRequest(upload, 5);
            ASSERT_EQ(preview.first, drogon::ReqResult::Ok);
            ASSERT_EQ(preview.second->statusCode(), 201) << preview.second->body();
            auto task = Json::parse(preview.second->body().begin(), preview.second->body().end())
                            .at("data");
            ASSERT_EQ(task.at("status"), "confirmed") << task;
            auto apply = call("/api/point-imports/apply", drogon::Post,
                              {{"import_job_id", task.at("import_job_id")},
                               {"expected_preview_hash", task.at("preview_hash")},
                               {"expected_config_revision", "2"},
                               {"mode", "merge"}});
            ASSERT_EQ(apply->statusCode(), 200) << apply->body();
            EXPECT_EQ(
                Json::parse(apply->body().begin(), apply->body().end()).at("data").at("status"),
                "applied");

            // 与浏览器整站上传完全相同：ZIP + 空 metadata，不要求 address_base。
            auto station_upload = drogon::HttpRequest::newHttpRequest();
            station_upload->setPath("/api/station-imports/inspect");
            station_upload->setMethod(drogon::Post);
            station_upload->setContentTypeString("multipart/form-data; boundary=" + boundary);
            station_upload->addHeader("Origin", options.allowed_origin);
            station_upload->addHeader("Cookie", cookie);
            station_upload->addHeader("X-CSRF-Token", csrf);
            station_upload->setBody(
                "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"metadata\"\r\n\r\n{}\r\n--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"file\"; "
                "filename=\"station.zip\"\r\nContent-Type: application/zip\r\n\r\n" +
                station_bytes + "\r\n--" + boundary + "--\r\n");
            auto station_inspection = client->sendRequest(station_upload, 60);
            ASSERT_EQ(station_inspection.first, drogon::ReqResult::Ok);
            ASSERT_EQ(station_inspection.second->statusCode(), 200)
                << station_inspection.second->body();
            const auto inspection = Json::parse(station_inspection.second->body().begin(),
                                                station_inspection.second->body().end());
            EXPECT_EQ(inspection.at("meta").at("config_revision"), "3");
            EXPECT_EQ(inspection.at("data").at("station_code_match"),
                      expected_station_devices == 1);
            auto station_children =
                call("/api/devices/tree", drogon::Post, {{"parent_device_id", "station"}});
            EXPECT_EQ(Json::parse(station_children->body().begin(), station_children->body().end())
                          .at("data")
                          .at("items")
                          .size(),
                      1);
            auto confirmed_upload = drogon::HttpRequest::newHttpRequest();
            confirmed_upload->setPath("/api/station-imports/create");
            confirmed_upload->setMethod(drogon::Post);
            confirmed_upload->setContentTypeString("multipart/form-data; boundary=" + boundary);
            confirmed_upload->addHeader("Origin", options.allowed_origin);
            confirmed_upload->addHeader("Cookie", cookie);
            confirmed_upload->addHeader("X-CSRF-Token", csrf);
            confirmed_upload->setBody(std::string(station_upload->body()));
            auto station_result = client->sendRequest(confirmed_upload, 60);
            ASSERT_EQ(station_result.first, drogon::ReqResult::Ok);
            ASSERT_EQ(station_result.second->statusCode(), 201) << station_result.second->body();
            auto station_data = Json::parse(station_result.second->body().begin(),
                                            station_result.second->body().end())
                                    .at("data");
            EXPECT_EQ(station_data.at("station_code_match"), expected_station_devices == 1);
            EXPECT_EQ(station_data.at("devices_created"), expected_station_devices);
            EXPECT_EQ(station_data.at("import_jobs").size(), expected_station_jobs);
            for (const auto& job : station_data.at("import_jobs")) {
                EXPECT_EQ(job.at("status"), "applied") << job;
                EXPECT_EQ(job.at("row_error"), 0) << job;
            }
            call("/api/auth/logout", drogon::Post, Json::object());
            EXPECT_EQ(call("/api/auth/session", drogon::Get, {})->statusCode(), 401);
        } catch (const std::exception& error) {
            ADD_FAILURE() << error.what();
        }
    });
    run_web_with_database(options, db, identity);
    client_thread.join();
}

TEST_F(WebDevice, HttpStartsWithoutAccounts) {
    WebOptions options;
    options.port = 19763;
    options.allowed_origin = "http://127.0.0.1:19763";
    options.workspace = (directory / "http-upload").u8string();

    // CTest 为各 HTTP 场景启动独立进程，避免复用 Drogon 的全局事件循环。
    (void)drogon::app();
    std::thread client_thread([&] {
        struct QuitOnExit {
            ~QuitOnExit() { drogon::app().quit(); }
        } quit;
        try {
            for (int i = 0; i < 200 && !drogon::app().isRunning(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ASSERT_TRUE(drogon::app().isRunning());
            auto client = drogon::HttpClient::newHttpClient(options.allowed_origin);
            auto call = [&](const std::string& path, drogon::HttpMethod method,
                            const Json& body = Json::object()) {
                auto request = drogon::HttpRequest::newHttpRequest();
                request->setPath(path);
                request->setMethod(method);
                request->addHeader("Origin", options.allowed_origin);
                if (method == drogon::Post) {
                    request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    request->setBody(body.dump());
                }
                const auto result = client->sendRequest(request, 5);
                if (result.first != drogon::ReqResult::Ok)
                    throw std::runtime_error("HTTP transport failed");
                return result.second;
            };
            EXPECT_EQ(call("/health/live", drogon::Get)->statusCode(), 200);
            EXPECT_EQ(call("/health/ready", drogon::Get)->statusCode(), 200);
            auto login = call("/api/auth/login", drogon::Post,
                              {{"username", "operator"}, {"password", "any-password"}});
            EXPECT_EQ(login->statusCode(), 503);
            EXPECT_EQ(
                Json::parse(login->body().begin(), login->body().end()).at("error").at("reason"),
                "identity_not_configured");
            EXPECT_EQ(call("/api/devices/list", drogon::Post)->statusCode(), 401);
        } catch (const std::exception& error) {
            ADD_FAILURE() << error.what();
        }
    });
    run_web_with_database(options, db);
    client_thread.join();
}

TEST_F(WebDevice, RuntimeConfigurationDoesNotContainAccounts) {
    const auto file = directory / "web.json";
    auto load = [&](const Json& configuration) {
        std::ofstream output(file);
        output << configuration.dump();
        output.close();
        return load_web_options(file.u8string());
    };
    const auto options = load(Json::object());
    EXPECT_EQ(options.host, "127.0.0.1");
    EXPECT_EQ(options.port, 8080);
    EXPECT_THROW(load(Json{{"users", Json::array()}}), ApiError);
    EXPECT_THROW(load(Json{{"identity_file", "accounts.json"}}), ApiError);
}

TEST(WebIdentity, UnconfiguredAccountsDoNotBlockConstructionOrGrantAccess) {
    SessionService sessions(Json::array(), 10, 60);
    try {
        sessions.login({{"username", "operator"}, {"password", "any-password"}});
        FAIL();
    } catch (const ApiError& error) {
        EXPECT_EQ(error.status, 503);
        EXPECT_EQ(error.reason, "identity_not_configured");
    }
    try {
        sessions.get("arbitrary-token");
        FAIL();
    } catch (const ApiError& error) {
        EXPECT_EQ(error.status, 401);
    }
}

TEST(WebExecutor, BoundedAdmissionAndDrain) {
    BoundedExecutor executor(1, 1);
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<int> completed{0};
    ASSERT_TRUE(executor.submit([&] {
        entered.set_value();
        released.wait();
        ++completed;
    }));
    entered.get_future().wait();
    EXPECT_TRUE(executor.submit([&] { ++completed; }));
    EXPECT_FALSE(executor.submit([] {}));
    executor.stop();
    EXPECT_FALSE(executor.submit([] {}));
    release.set_value();
    executor.wait();
    EXPECT_EQ(completed.load(), 2);
    EXPECT_TRUE(executor.idle());
}

TEST(WebIdentity, PasswordHashAndSessionLifecycle) {
    auto encoded = hash_password("test-long-password");
    EXPECT_TRUE(verify_password("test-long-password", encoded));
    EXPECT_FALSE(verify_password("wrong", encoded));
    SessionService sessions(Json::array({{{"username", "operator"},
                                          {"password_hash", encoded},
                                          {"permissions", Json::array({"device.read"})},
                                          {"station_ids", Json::array({"station"})},
                                          {"device_roots", Json::array({"*"})}}}),
                            10, 60);
    auto login = sessions.login({{"username", "operator"}, {"password", "test-long-password"}});
    auto current = sessions.get(login.first);
    EXPECT_EQ(current.principal.subject_id, "operator");
    EXPECT_EQ(current.csrf.size(), 64u);
    EXPECT_THROW(current.principal.require("device.write"), ApiError);
    sessions.logout(login.first);
    EXPECT_THROW(sessions.get(login.first), ApiError);
}
}  // namespace
}  // namespace ems::web
