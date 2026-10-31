#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <ormpp/sqlite.hpp>

#include "common/db/configuration/orm_device_management_store.h"
#include "common/db/identity/user_repository.h"
#include "common/db/runtime/transaction_database.h"
#include "web/app/web_options.h"
#include "web/modules/device/application/device_service.h"
#include "web/modules/identity/api/identity_codec.h"
#include "web/modules/identity/module.h"
#include "web/platform/crypto.h"
#include "web/platform/host_credentials.h"

namespace ems::web {
namespace {
using namespace database;

class IdentityConnection final : public detail::Connection {
   public:
    ormpp::sqlite orm;

    core::Status prepare(const core::CallContext& ctx) {
        return core::has_expired(ctx)
                   ? core::Status{core::make_error(core::ErrorCode::DeadlineExceeded,
                                                   core::WriteEffect::NotApplied)}
                   : core::ok_status();
    }

    core::Status status(bool ok) {
        return ok ? core::ok_status()
                  : core::Status{
                        core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied)};
    }

    core::Status begin(const core::CallContext&) override { return status(orm.begin()); }

    core::Status commit(const core::CallContext&) override { return status(orm.commit()); }

    core::Status rollback(const core::CallContext&) override { return status(orm.rollback()); }
};

std::string file_text(const std::string& path) {
    std::ifstream input(std::filesystem::u8path(path));
    return {(std::istreambuf_iterator<char>(input)), {}};
}

class PersistentIdentity : public testing::Test {
   protected:
    std::filesystem::path dir;
    MySqlDatabaseContext db;
    std::shared_ptr<detail::TransactionDatabase> runtime;
    std::shared_ptr<AuthorizationService> auth;
    std::unique_ptr<SessionService> sessions;
    std::unique_ptr<UserService> users;
    std::string admin_token, admin_id;
    const std::string initial = "InitialSafe!2026", permanent = "ChangedSafe!2026";

    IdentityCall call() {
        return {core::make_call_context(std::chrono::seconds(30), random_token()), "127.0.0.1",
                "Chrome/120 Windows"};
    }

    void sql(const std::string& command) {
        sqlite3* native = nullptr;
        ASSERT_EQ(sqlite3_open((dir / "ems.db").u8string().c_str(), &native), SQLITE_OK);
        std::string attach = "ATTACH DATABASE '" + (dir / "web.db").generic_u8string() +
                             "' AS ems_web; PRAGMA foreign_keys=ON;";
        char* error = nullptr;
        ASSERT_EQ(sqlite3_exec(native, (attach + command).c_str(), nullptr, nullptr, &error),
                  SQLITE_OK)
            << (error ? error : "");
        sqlite3_free(error);
        sqlite3_close(native);
    }

    void SetUp() override {
        dir = std::filesystem::temp_directory_path() / random_token();
        std::filesystem::create_directories(dir);
        sql(file_text(EMS_WEB_SCHEMA_FILE) + file_text(EMS_IDENTITY_SCHEMA_FILE));
        auto file = (dir / "ems.db").u8string(), web = (dir / "web.db").generic_u8string();
        runtime = std::make_shared<detail::TransactionDatabase>(
            detail::TransactionOptions{},
            [file, web](const core::CallContext&) -> core::Result<detail::ConnectionLease> {
                auto c = std::make_shared<IdentityConnection>();
                if (!c->orm.connect("", "", "", file, {}, {}))
                    return core::make_error(core::ErrorCode::Unavailable,
                                            core::WriteEffect::NotApplied);
                if (!c->orm.execute("ATTACH DATABASE '" + web + "' AS ems_web") ||
                    !c->orm.execute("PRAGMA foreign_keys=ON"))
                    return core::make_error(core::ErrorCode::Internal,
                                            core::WriteEffect::NotApplied);
                c->orm.set_enable_transaction(false);
                return detail::ConnectionLease{c, [](std::shared_ptr<detail::Connection>, bool) {}};
            });
        auto acquire = [runtime = runtime](
                           ITransaction& tx,
                           const core::CallContext& ctx) -> core::Result<IdentityConnection*> {
            auto c = runtime->connection(tx, ctx);
            if (!core::is_ok(c)) return core::error_of(c);
            return static_cast<IdentityConnection*>(core::value_of(c));
        };
        auto invalidate = [runtime = runtime](ITransaction& tx) { runtime->invalidate(tx); };
        auto failure = [](IdentityConnection&) {
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplied);
        };
        auto repository = std::make_shared<detail::OrmIdentityRepository<IdentityConnection>>(
            acquire, invalidate, failure, false);
        db.database = runtime;
        db.users = repository;
        db.audit = repository;
        db.device_management =
            std::make_shared<detail::OrmDeviceManagementStore<IdentityConnection>>(
                acquire, invalidate, failure, false);
        {
            auto ctx = call().context;
            auto tx = take(db.database->begin(ctx));
            for (const auto& id : {"station-main", "unit-a", "unit-b", "device-a", "device-b"}) {
                CfgDeviceRow node;
                node.device_id = id;
                node.code = id;
                node.name = id;
                node.node_kind = std::string(id).rfind("station", 0) == 0 ? "station"
                                 : std::string(id).rfind("unit", 0) == 0  ? "unit"
                                                                          : "device";
                node.parent_device_id = node.node_kind == "station"     ? ""
                                        : node.node_kind == "unit"      ? "station-main"
                                        : std::string(id) == "device-a" ? "unit-a"
                                                                        : "unit-b";
                node.path = "/" + node.device_id;
                node.timezone = "Asia/Shanghai";
                node.config_revision = 1;
                take(db.device_management->put(*tx, node, ctx));
            }
            take(tx->commit(ctx));
        }
        auth = std::make_shared<AuthorizationService>(db, std::string(32, 'k'));
        sessions = std::make_unique<SessionService>(auth);
        users = std::make_unique<UserService>(auth);
        auth->check_schema(call().context);
        auto admin = users->provision("supervisor", "管理员", initial, false, call());
        admin_id = admin.at("user_id");
        auto first = sessions->login({{"username", "supervisor"}, {"password", initial}}, call());
        sessions->execute("/api/auth/password/complete", first.first,
                          {{"old_password", initial}, {"new_password", permanent}}, call());
        admin_token =
            sessions->login({{"username", "supervisor"}, {"password", permanent}}, call()).first;
    }

    void TearDown() override {
        users.reset();
        sessions.reset();
        auth.reset();
        db = {};
        runtime.reset();
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    Json execute(const std::string& route, const Json& body = Json::object(),
                 const std::string& token = "") {
        return users->execute(route, token.empty() ? admin_token : token,
                              body.is_null() ? Json::object() : body, call());
    }

    Json create(const std::string& name = "observer1", const std::string& role = "observer",
                const std::string& status = "disabled") {
        return execute("/api/users/create", {{"username", name},
                                             {"display_name", "测试用户"},
                                             {"temporary_password", initial},
                                             {"role_code", role},
                                             {"status", status},
                                             {"scope_mode", "selected"},
                                             {"scopes", Json::array()}});
    }

    Json detail(const Json& user) {
        return execute("/api/users/detail", {{"user_id", user.at("user_id")}});
    }

    Json change(const std::string& action, const Json& user, Json extra = Json::object()) {
        extra["user_id"] = user.at("user_id");
        extra["expected_edit_token"] = user.at("edit_token");
        return execute("/api/users/" + action, extra);
    }

    std::string login_user(const std::string& name) {
        auto first = sessions->login({{"username", name}, {"password", initial}}, call());
        sessions->execute("/api/auth/password/complete", first.first,
                          {{"old_password", initial}, {"new_password", permanent}}, call());
        return sessions->login({{"username", name}, {"password", permanent}}, call()).first;
    }

    template <class F>
    void error(F fn, int status, const std::string& reason) {
        try {
            fn();
            FAIL() << "expected rejection";
        } catch (const ApiError& e) {
            EXPECT_EQ(e.status, status);
            EXPECT_EQ(e.reason, reason);
        }
    }
};

TEST_F(PersistentIdentity, PersistentCookieRestrictedFirstPasswordAndNoSecrets) {
    auto user = create("observer1", "observer", "enabled");
    auto first = sessions->login({{"username", "observer1"}, {"password", initial}}, call());
    EXPECT_EQ(first.second.data.at("kind"), "password_change");
    EXPECT_FALSE(first.second.data.contains("permissions"));
    EXPECT_EQ(first.second.csrf.size(), 43u);
    error([&] { execute("/api/profile/me", {}, first.first); }, 403, "password_change_required");
    sessions->execute("/api/auth/password/complete", first.first,
                      {{"old_password", initial}, {"new_password", permanent}}, call());
    error([&] { sessions->get(first.first, call()); }, 401, "unauthenticated");
    auto token =
        sessions->login({{"username", "observer1"}, {"password", permanent}}, call()).first;
    SessionService restarted(auth);
    auto restored = restarted.get(token, call());
    EXPECT_EQ(restored.principal.role_code, "observer");
    EXPECT_FALSE(restored.principal.permissions.count("*"));
    EXPECT_FALSE(restored.principal.permissions.count("channel.read"));
    EXPECT_TRUE(restored.principal.permissions.count("device.read"));
    auto tx = take(db.database->begin(call().context));
    auto row =
        take(db.users->user(*tx, user.at("user_id"), false, IdentityLock::None, call().context));
    EXPECT_EQ(row->password_hash.rfind("$argon2id$", 0), 0u);
    auto s = take(db.users->session(*tx, digest_bytes(sha256(token)), call().context));
    ASSERT_TRUE(s);
    EXPECT_EQ(s->token_hash.size(), 32u);
    EXPECT_EQ(user.dump().find("password_hash"), std::string::npos);
    EXPECT_EQ(restored.data.dump().find(token), std::string::npos);
}

TEST_F(PersistentIdentity, RoleIsolationSelfRulesAndMassAssignment) {
    auto user = create("operator1", "operator", "enabled");
    auto token = login_user("operator1");
    auto state = sessions->get(token, call());
    EXPECT_TRUE(state.principal.permissions.count("command.yk"));
    EXPECT_FALSE(state.principal.permissions.count("device.write"));
    error([&] { execute("/api/users/list", {}, token); }, 403, "permission_denied");
    error(
        [&] {
            execute("/api/profile/update", {{"display_name", "伪造"}, {"role_code", "admin"}},
                    token);
        },
        400, "unknown_field");
    auto profile = execute("/api/profile/update", {{"display_name", std::string(64, 'a')}}, token);
    EXPECT_EQ(profile.at("display_name"), std::string(64, 'a'));
    error([&] { change("set-status", detail({{"user_id", admin_id}}), {{"status", "disabled"}}); },
          422, "self_management_forbidden");
    error([&] { execute("/api/users/detail", {}); }, 400, "invalid_argument");
    error([&] { execute("/api/users/reset-password", {{"temporary_password", initial}}); }, 400,
          "invalid_argument");
}

TEST_F(PersistentIdentity, FailureCountersCommitAndPolicyAppliesToExistingSessions) {
    auto user = create("observer1", "observer", "enabled");
    for (int i = 0; i < 6; ++i)
        error(
            [&] {
                sessions->login({{"username", "observer1"}, {"password", "WrongSafe!2026"}},
                                call());
            },
            401, "invalid_credentials");
    auto locked = detail(user);
    EXPECT_FALSE(locked.at("locked_until").is_null());
    error([&] { sessions->login({{"username", "observer1"}, {"password", initial}}, call()); }, 401,
          "invalid_credentials");
    auto tx = take(db.database->begin(call().context));
    auto row =
        take(db.users->user(*tx, user.at("user_id"), false, IdentityLock::None, call().context));
    ASSERT_TRUE(row);
    EXPECT_EQ(row->failed_attempts, 6);
    take(tx->rollback(call().context));
    change("unlock", locked);
    EXPECT_TRUE(detail(user).at("locked_until").is_null());
    auto policy = execute("/api/users/security-settings");
    auto body = policy;
    body["expected_edit_token"] = body.at("edit_token");
    body.erase("edit_token");
    body["idle_timeout_minutes"] = 20;
    auto saved = execute("/api/users/security-settings/update", body);
    EXPECT_EQ(saved.at("idle_timeout_minutes"), 20);
    error([&] { execute("/api/users/security-settings/update", body); }, 409, "edit_conflict");
    sql("UPDATE ems_web.auth_session SET last_interactive_at='2000-01-01 00:00:00.000' WHERE "
        "kind='full'");
    error([&] { sessions->get(admin_token, call()); }, 401, "unauthenticated");
}

TEST_F(PersistentIdentity, EditConflictsDeleteReservationAndUnfilteredStats) {
    auto first = create();
    auto stale = first;
    auto changed = change("update", first, {{"display_name", "新的名字"}});
    error([&] { change("update", stale, {{"display_name", "旧表单"}}); }, 409, "edit_conflict");
    auto list =
        execute("/api/users/list", {{"keyword", "observer1"}, {"page", 1}, {"page_size", 1}});
    EXPECT_EQ(list.at("total"), 1);
    EXPECT_EQ(list.at("stats").at("total"), 2);
    change("delete", changed);
    error([&] { create(); }, 409, "username_conflict");
    error([&] { detail(first); }, 404, "user_not_found");
    EXPECT_EQ(execute("/api/users/list").at("stats").at("total"), 1);
}

TEST_F(PersistentIdentity, AccessChangesRevokeSessionsAndRecentAuthenticationRequired) {
    auto user = create("operator1", "operator", "enabled");
    auto token = login_user("operator1");
    auto current = detail(user);
    change("set-access", current,
           {{"role_code", "observer"}, {"scope_mode", "selected"}, {"scopes", Json::array()}});
    error([&] { sessions->get(token, call()); }, 401, "unauthenticated");
    auto other = create("observer2");
    sql("UPDATE ems_web.auth_session SET auth_time='2000-01-01 00:00:00.000' WHERE kind='full'");
    error([&] { change("set-status", other, {{"status", "enabled"}}); }, 403,
          "reauthentication_required");
    sessions->execute("/api/auth/reauthenticate", admin_token, {{"password", permanent}}, call());
    auto enabled = change("set-status", other, {{"status", "enabled"}});
    EXPECT_EQ(enabled.at("status"), "enabled");
}

TEST_F(PersistentIdentity, SessionLimitOwnershipIdempotenceAndPollingDoesNotRenew) {
    auto user = create("observer1", "observer", "enabled");
    auto token = login_user("observer1");
    auto initial_state = sessions->get(token, call());
    auto before = execute("/api/profile/sessions/list", {}, token)
                      .at("items")
                      .at(0)
                      .at("last_interactive_at");
    sessions->get(token, call());
    EXPECT_EQ(execute("/api/profile/sessions/list", {}, token)
                  .at("items")
                  .at(0)
                  .at("last_interactive_at"),
              before);
    error(
        [&] {
            execute("/api/profile/sessions/revoke",
                    {{"session_id", sessions->get(admin_token, call()).principal.session_id}},
                    token);
        },
        404, "session_not_found");
    for (int i = 0; i < 3; ++i)
        sessions->login({{"username", "observer1"}, {"password", permanent}}, call());
    error([&] { sessions->get(token, call()); }, 401, "unauthenticated");
    auto list = execute("/api/users/sessions/list", {{"user_id", user.at("user_id")}});
    EXPECT_EQ(list.at("total"), 3);
    auto sid = list.at("items").at(0).at("session_id");
    execute("/api/users/sessions/revoke", {{"user_id", user.at("user_id")}, {"session_id", sid}});
    execute("/api/users/sessions/revoke", {{"user_id", user.at("user_id")}, {"session_id", sid}});
    EXPECT_EQ(execute("/api/users/sessions/list", {{"user_id", user.at("user_id")}}).at("total"),
              2);
}

TEST_F(PersistentIdentity, AuditFailureRollsBackUserAndFailedLoginCounter) {
    auto user = create("observer1", "observer", "enabled");
    sql("CREATE TRIGGER ems_web.reject_audit BEFORE INSERT ON audit_log BEGIN SELECT "
        "RAISE(ABORT,'test'); END;");
    error([&] { change("update", user, {{"display_name", "不应保存"}}); }, 503,
          "audit_unavailable");
    EXPECT_EQ(detail(user).at("display_name"), user.at("display_name"));
    error(
        [&] {
            sessions->login({{"username", "observer1"}, {"password", "WrongSafe!2026"}}, call());
        },
        503, "audit_unavailable");
    auto tx = take(db.database->begin(call().context));
    auto row =
        take(db.users->user(*tx, user.at("user_id"), false, IdentityLock::None, call().context));
    EXPECT_EQ(row->failed_attempts, 0);
}

TEST_F(PersistentIdentity, ScopeValidationAndCrossSchemaRollback) {
    auto capabilities = execute("/api/users/capabilities");
    EXPECT_FALSE(capabilities.at("scope_tree").empty());
    auto user = create();
    error(
        [&] {
            change("set-access", user,
                   {{"role_code", "observer"}, {"scope_mode", "all"}, {"scopes", Json::array()}});
        },
        422, "invalid_scope");
    error(
        [&] {
            change("set-access", user,
                   {{"role_code", "observer"},
                    {"scope_mode", "selected"},
                    {"scopes", Json::array({{{"station_id", "station-does-not-exist"},
                                             {"device_root_id", nullptr}}})}});
        },
        422, "invalid_scope");
}

TEST_F(PersistentIdentity, SubtreePairingNormalizationAndRevocationBeforeDeviceWrite) {
    auto user = create("observer1", "observer", "enabled");
    auto granted =
        change("set-access", user,
               {{"role_code", "observer"},
                {"scope_mode", "selected"},
                {"scopes",
                 Json::array({{{"station_id", "station-main"}, {"device_root_id", "unit-a"}},
                              {{"station_id", "station-main"}, {"device_root_id", "device-a"}},
                              {{"station_id", "station-main"}, {"device_root_id", "unit-a"}}})}});
    EXPECT_EQ(granted.at("scopes").size(), 1u);
    auto token = login_user("observer1");
    auto actor = sessions->get(token, call()).principal;
    {
        DeviceRequest request(db, actor, call().context, false);
        EXPECT_EQ(request.device("device-a").at("device_id"), "device-a");
        error([&] { request.device("device-b"); }, 404, "device_not_found");
        error([&] { request.device("station-main"); }, 404, "device_not_found");
        EXPECT_TRUE(request.navigation_visible("station-main"));
    }
    auto current = detail(user);
    auto whole = change(
        "set-access", current,
        {{"role_code", "observer"},
         {"scope_mode", "selected"},
         {"scopes",
          Json::array({{{"station_id", "station-main"}, {"device_root_id", nullptr}},
                       {{"station_id", "station-main"}, {"device_root_id", "station-main"}}})}});
    ASSERT_EQ(whole.at("scopes").size(), 1u);
    EXPECT_TRUE(whole.at("scopes").at(0).at("device_root_id").is_null());
    error([&] { DeviceRequest request(db, actor, call().context, true); }, 401, "unauthenticated");
}

TEST_F(PersistentIdentity, HttpPersistentUserLifecycleAndCsrf) {
    WebOptions options;
    options.port = 19764;
    options.allowed_origin = "http://127.0.0.1:19764";
    options.workspace = (dir / "http-upload").u8string();
    auto identity = std::make_shared<SessionService>(auth);
    (void)drogon::app();
    std::thread client_thread([&] {
        struct Quit {
            ~Quit() { drogon::app().quit(); }
        } quit;
        try {
            for (int i = 0; i < 200 && !drogon::app().isRunning(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ASSERT_TRUE(drogon::app().isRunning());
            auto client = drogon::HttpClient::newHttpClient(options.allowed_origin);
            struct Browser {
                std::string cookie, csrf;
            };
            Browser admin, observer, anonymous;
            auto request = [&](const std::string& path, drogon::HttpMethod method, const Json& body,
                               const Browser& browser, bool csrf = true,
                               const std::string& origin = "") {
                auto req = drogon::HttpRequest::newHttpRequest();
                req->setPath(path);
                req->setMethod(method);
                req->addHeader("Origin", origin.empty() ? options.allowed_origin : origin);
                if (method == drogon::Post) {
                    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
                    req->setBody(body.dump());
                }
                if (!browser.cookie.empty()) req->addHeader("Cookie", browser.cookie);
                if (csrf && !browser.csrf.empty()) req->addHeader("X-CSRF-Token", browser.csrf);
                auto response = client->sendRequest(req, 10);
                if (response.first != drogon::ReqResult::Ok)
                    throw std::runtime_error("HTTP transport failed");
                return response.second;
            };
            auto data = [](const drogon::HttpResponsePtr& response) {
                return Json::parse(response->body().begin(), response->body().end()).at("data");
            };
            auto login = [&](Browser& browser, const std::string& name,
                             const std::string& password) {
                auto response = request("/api/auth/login", drogon::Post,
                                        {{"username", name}, {"password", password}}, anonymous);
                if (response->statusCode() != 200) throw std::runtime_error("HTTP login failed");
                auto cookie = response->getHeader("set-cookie");
                browser.cookie = cookie.substr(0, cookie.find(';'));
                if (browser.cookie.empty() && !response->cookies().empty()) {
                    auto saved = response->cookies().begin()->second;
                    browser.cookie = saved.key() + "=" + saved.value();
                }
                browser.csrf = data(response).at("csrf_token");
                return response;
            };
            EXPECT_EQ(request("/health/ready", drogon::Get, {}, anonymous)->statusCode(), 200);
            EXPECT_EQ(request("/api/auth/login-options", drogon::Get, {}, anonymous)->statusCode(),
                      200);
            EXPECT_EQ(request("/api/auth/login", drogon::Get, {}, anonymous)->statusCode(), 405);
            EXPECT_EQ(request("/api/auth/login", drogon::Post,
                              {{"username", "supervisor"}, {"password", permanent}}, anonymous,
                              true, "https://invalid.example")
                          ->statusCode(),
                      403);
            auto signed_in = login(admin, "supervisor", permanent);
            EXPECT_EQ(data(signed_in).at("kind"), "full");
            EXPECT_EQ(admin.csrf.size(), 43u);
            EXPECT_EQ(signed_in->getHeader("cache-control"), "no-store");
            EXPECT_EQ(request("/api/users/list", drogon::Post, Json::object(), admin, false)
                          ->statusCode(),
                      403);
            EXPECT_EQ(request("/api/users/list", drogon::Post, Json::object(), admin)->statusCode(),
                      200);
            auto create = request("/api/users/create", drogon::Post,
                                  {{"username", "http_observer"},
                                   {"display_name", "观察员"},
                                   {"temporary_password", initial},
                                   {"status", "enabled"}},
                                  admin);
            ASSERT_EQ(create->statusCode(), 201);
            auto user = data(create);
            EXPECT_EQ(user.at("role_code"), "observer");
            EXPECT_FALSE(user.contains("password_hash"));
            EXPECT_FALSE(user.contains("temporary_password"));
            auto restricted = login(observer, "http_observer", initial);
            EXPECT_EQ(data(restricted).at("kind"), "password_change");
            EXPECT_EQ(request("/api/profile/me", drogon::Get, {}, observer)->statusCode(), 403);
            EXPECT_EQ(
                request("/api/auth/reauthenticate", drogon::Post, {{"password", initial}}, observer)
                    ->statusCode(),
                403);
            auto changed =
                request("/api/auth/password/complete", drogon::Post,
                        {{"old_password", initial}, {"new_password", permanent}}, observer);
            ASSERT_EQ(changed->statusCode(), 200);
            EXPECT_TRUE(data(changed).at("reauthentication_required"));
            EXPECT_EQ(request("/api/auth/session", drogon::Get, {}, observer)->statusCode(), 401);
            login(observer, "http_observer", permanent);
            EXPECT_EQ(
                request("/api/users/list", drogon::Post, Json::object(), observer)->statusCode(),
                403);
            EXPECT_EQ(
                request("/api/channels/list", drogon::Post, Json::object(), observer)->statusCode(),
                403);
            auto profile = request("/api/profile/update", drogon::Post,
                                   {{"display_name", "新的观察员"}}, observer);
            ASSERT_EQ(profile->statusCode(), 200);
            EXPECT_EQ(data(profile).at("display_name"), "新的观察员");
            user = data(request("/api/users/detail", drogon::Post,
                                {{"user_id", user.at("user_id")}}, admin));
            auto access = request(
                "/api/users/set-access", drogon::Post,
                {{"user_id", user.at("user_id")},
                 {"expected_edit_token", user.at("edit_token")},
                 {"role_code", "operator"},
                 {"scope_mode", "selected"},
                 {"scopes",
                  Json::array({{{"station_id", "station-main"}, {"device_root_id", nullptr}}})}},
                admin);
            ASSERT_EQ(access->statusCode(), 200);
            user = data(access);
            EXPECT_EQ(request("/api/auth/session", drogon::Get, {}, observer)->statusCode(), 401);
            auto granted = login(observer, "http_observer", permanent);
            EXPECT_EQ(data(granted).at("user").at("role_code"), "operator");
            auto reset = request("/api/users/reset-password", drogon::Post,
                                 {{"user_id", user.at("user_id")},
                                  {"expected_edit_token", user.at("edit_token")},
                                  {"temporary_password", "ResetSafe!2026"}},
                                 admin);
            ASSERT_EQ(reset->statusCode(), 200);
            EXPECT_EQ(request("/api/profile/me", drogon::Get, {}, observer)->statusCode(), 401);
            EXPECT_EQ(data(login(observer, "http_observer", "ResetSafe!2026")).at("kind"),
                      "password_change");
            EXPECT_EQ(
                request("/api/auth/logout", drogon::Post, Json::object(), observer)->statusCode(),
                200);
            EXPECT_EQ(
                request("/api/auth/logout", drogon::Post, Json::object(), anonymous)->statusCode(),
                200);
            auto policy = data(request("/api/users/security-settings", drogon::Get, {}, admin));
            policy["expected_edit_token"] = policy.at("edit_token");
            policy.erase("edit_token");
            policy["login_notice"] = "通知\n请妥善保管密码";
            EXPECT_EQ(request("/api/users/security-settings/update", drogon::Post, policy, admin)
                          ->statusCode(),
                      200);
            EXPECT_EQ(data(request("/api/auth/login-options", drogon::Get, {}, anonymous))
                          .at("login_notice"),
                      "通知\n请妥善保管密码");
        } catch (const std::exception& e) {
            ADD_FAILURE() << e.what();
        }
    });
    run_web_with_database(options, db, identity);
    client_thread.join();
}

TEST(IdentityCodec, UnicodeLengthWeakPasswordAndCryptoBounds) {
    std::string name;
    for (int i = 0; i < 64; ++i) name += "陈";
    EXPECT_EQ(identity_text({{"name", name}}, "name", 64), name);
    EXPECT_THROW(identity_text({{"name", name + "陈"}}, "name", 64), ApiError);
    EXPECT_THROW(identity_text({{"name", std::string("\xc0\xaf", 2)}}, "name", 64), ApiError);
    EXPECT_THROW(validate_password("Password123!", "somebody", 12), ApiError);
    EXPECT_FALSE(verify_password("secret", "$argon2id$v=19$m=999999999,t=3,p=1$broken$broken"));
    EXPECT_EQ(random_csrf().size(), 43u);
    EXPECT_EQ(hmac_sha256("key", "The quick brown fox jumps over the lazy dog"),
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST(IdentityHost, KeyCreationNeverOverwritesExistingKey) {
    auto path = std::filesystem::temp_directory_path() / random_token();
    create_identity_key(path.u8string());
    auto key = load_identity_key(path.u8string());
    EXPECT_EQ(key.size(), 64u);
    EXPECT_THROW(create_identity_key(path.u8string()), std::exception);
    EXPECT_EQ(load_identity_key(path.u8string()), key);
    std::filesystem::remove(path);
}
}  // namespace
}  // namespace ems::web
