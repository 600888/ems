#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <ormpp/sqlite.hpp>
#include <util/excel.hpp>

#include "common/db/configuration/orm_device_management_store.h"
#include "common/db/runtime/transaction_database.h"
#include "web/app/web_options.h"
#include "web/modules/device/application/device_service.h"
#include "web/modules/identity/application/session_service.h"
#include "web/platform/crypto.h"
#include "web/platform/executor/bounded_executor.h"

namespace ems::web {
using namespace database;

namespace {
class TestConnection final : public detail::Connection {
   public:
    ormpp::sqlite orm;

    core::Status prepare(const core::CallContext& ctx) {
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
};

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
        "order,scale,offset,unit\nreg0,,Temperature,holding_register,0,read,i16,,0.1,0,C\n";
    Json metadata = {{"device_id", "hvac01"},
                     {"binding_id", "binding01"},
                     {"source_kind", "vendor_csv"},
                     {"address_base", "zero"}};
    auto preview =
        service->execute("/api/point-imports/create", metadata, actor, context(), upload);
    EXPECT_EQ(preview.status, 201);
    EXPECT_EQ(preview.meta.at("config_revision"), "2");
    EXPECT_EQ(preview.data.at("status"), "confirmed") << preview.data;
    const auto id = preview.data.at("import_job_id");
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
    EXPECT_EQ(list.data.at("total"), 1);
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
          std::int64_t{0}, std::string("read"), std::string("i16"), std::string{}, 0.1, 0.0,
          std::string("C")}});
    excel.save();
    excel.close();

    std::ifstream input(path, std::ios::binary);
    Upload upload;
    upload.filename = "厂家点表.xlsx";
    upload.bytes.assign(std::istreambuf_iterator<char>(input), {});
    input.close();
    auto preview = service->execute("/api/point-imports/create",
                                    {{"device_id", "hvac01"},
                                     {"binding_id", "binding01"},
                                     {"source_kind", "vendor_xlsx"},
                                     {"address_base", "zero"}},
                                    actor, context(), upload);
    EXPECT_EQ(preview.data.at("status"), "confirmed") << preview.data;
    EXPECT_EQ(preview.meta.at("config_revision"), "2");
    auto rows =
        execute("/api/point-imports/rows", {{"import_job_id", preview.data.at("import_job_id")}});
    ASSERT_EQ(rows.data.at("total"), 1);
    EXPECT_EQ(rows.data.at("items").front().at("vendor_name"), "温度");

    // 重复表头必须保留解析错误，不能生成没有行的成功预览。
    excel.open(path);
    excel.writeCell("点表", 1, 7, std::string("access"));
    excel.save();
    excel.close();
    std::ifstream invalid_input(path, std::ios::binary);
    upload.bytes.assign(std::istreambuf_iterator<char>(invalid_input), {});
    EXPECT_NE(sha256(upload.bytes), preview.data.at("file_sha256"));
    const auto invalid = service->execute("/api/point-imports/create",
                                          {{"device_id", "hvac01"},
                                           {"binding_id", "binding01"},
                                           {"source_kind", "vendor_xlsx"},
                                           {"address_base", "zero"}},
                                          actor, context(), upload);
    EXPECT_EQ(invalid.data.at("status"), "failed");
    EXPECT_EQ(invalid.meta.at("config_revision"), "2");
}

TEST_F(WebDevice, HttpLoginCookieCsrfAndFixedJsonRoutes) {
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
                                 {"source_kind", "vendor_csv"},
                                 {"address_base", "zero"}}
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
