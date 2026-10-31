#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <future>

#include "web/modules/device/application/device_service.h"
#include "web/modules/identity/module.h"
#include "web/platform/crypto.h"

namespace ems::web {
TEST(IdentityMySql, PersistentStoreAndConcurrentRevocation) {
    const auto config = std::getenv("EMS_WEB_TEST_MYSQL_CONFIG");
    if (!config)
        GTEST_SKIP() << "Set EMS_WEB_TEST_MYSQL_CONFIG to an empty isolated MySQL instance";
    auto call = [] {
        return IdentityCall{core::make_call_context(std::chrono::seconds(30), random_token()),
                            "127.0.0.1", "test"};
    };
    auto loaded = database::load_mysql_options(config);
    ASSERT_TRUE(core::is_ok(loaded));
    auto options = core::value_of(loaded);
    ASSERT_EQ(options.database, "ems_identity_test")
        << "This test only operates on the isolated ems_identity_test + ems_web fixture";
    auto result = database::connect_mysql_database(options, call().context);
    ASSERT_TRUE(core::is_ok(result)) << (core::is_ok(result) ? "" : core::error_of(result).message);
    auto db = core::value_of(result);
    auto auth = std::make_shared<AuthorizationService>(db, std::string(32, 'k'));
    auth->check_schema(call().context);
    UserService users(auth);
    SessionService sessions(auth);
    auto initialize = [&] {
        try {
            users.provision("supervisor", "管理员", "InitialSafe!2026", false, call());
            return 201;
        } catch (const ApiError& e) {
            return e.status;
        }
    };
    auto initialization = std::async(std::launch::async, initialize);
    auto other_initialization = initialize();
    auto first_initialization = initialization.get();
    EXPECT_TRUE((first_initialization == 201 && other_initialization == 409) ||
                (first_initialization == 409 && other_initialization == 201));
    auto restricted =
        sessions.login({{"username", "supervisor"}, {"password", "InitialSafe!2026"}}, call());
    EXPECT_EQ(restricted.second.data.at("kind"), "password_change");
    sessions.execute("/api/auth/password/complete", restricted.first,
                     {{"old_password", "InitialSafe!2026"}, {"new_password", "ChangedSafe!2026"}},
                     call());
    {
        auto tx = take(db.database->begin(call().context));
        auto row = take(
            db.users->user(*tx, "supervisor", true, database::IdentityLock::None, call().context));
        ASSERT_TRUE(row);
        EXPECT_GT(row->id, 0);
        EXPECT_EQ(row->must_change_password, 0);
        EXPECT_TRUE(verify_password("ChangedSafe!2026", row->password_hash));
        take(tx->rollback(call().context));
    }
    auto token =
        sessions.login({{"username", "supervisor"}, {"password", "ChangedSafe!2026"}}, call())
            .first;
    auto execute = [&](const std::string& path, const Json& body) {
        return users.execute(path, token, body, call());
    };
    auto created = execute("/api/users/create", {{"username", "operator1"},
                                                 {"display_name", "操作员"},
                                                 {"role_code", "operator"},
                                                 {"status", "enabled"},
                                                 {"scope_mode", "selected"},
                                                 {"scopes", Json::array()},
                                                 {"temporary_password", "InitialSafe!2026"}});
    auto second =
        sessions.login({{"username", "operator1"}, {"password", "InitialSafe!2026"}}, call());
    sessions.execute("/api/auth/password/complete", second.first,
                     {{"old_password", "InitialSafe!2026"}, {"new_password", "ChangedSafe!2026"}},
                     call());
    auto operator_token =
        sessions.login({{"username", "operator1"}, {"password", "ChangedSafe!2026"}}, call()).first;
    SessionService restarted(auth);
    EXPECT_EQ(restarted.get(operator_token, call()).principal.role_code, "operator");
    auto current = execute("/api/users/detail", {{"user_id", created.at("user_id")}});
    auto tx = take(db.database->begin(call().context));
    auto principal = AuthorizationService::recheck(
        db, *tx, sessions.get(operator_token, call()).principal, call().context);
    EXPECT_TRUE(principal.permissions.count("command.yk"));
    auto revoke = std::async(std::launch::async, [&] {
        return execute("/api/users/set-status", {{"user_id", created.at("user_id")},
                                                 {"expected_edit_token", current.at("edit_token")},
                                                 {"status", "disabled"}});
    });
    EXPECT_EQ(revoke.wait_for(std::chrono::milliseconds(150)), std::future_status::timeout);
    take(tx->commit(call().context));
    EXPECT_EQ(revoke.get().at("status"), "disabled");
    EXPECT_THROW(restarted.get(operator_token, call()), ApiError);
    {
        auto list_tx = take(db.database->begin(call().context));
        database::UserQuery q;
        q.keyword = "操作";
        auto listing = db.users->users(*list_tx, q, identity_time(), call().context);
        ASSERT_TRUE(core::is_ok(listing))
            << (core::is_ok(listing) ? "" : core::error_of(listing).message);
        take(list_tx->rollback(call().context));
    }
    auto page = execute("/api/users/list", {{"keyword", "操作"}});
    EXPECT_EQ(page.at("total"), 1);
    EXPECT_EQ(page.at("stats").at("total"), 2);
    // 工程写入与 ems_web 审计通过同一个公共事务一起提交或回滚。
    {
        auto setup = take(db.database->begin(call().context));
        database::CfgDeviceRow root;
        root.device_id = "station";
        root.code = "station";
        root.node_kind = "station";
        root.name = "Original";
        root.path = "/";
        root.timezone = "Asia/Shanghai";
        root.created_at = root.updated_at = identity_time();
        take(db.device_management->put(*setup, root, call().context));
        take(setup->commit(call().context));
    }

    class RejectAudit final : public database::IAuditRepository {
       public:
        std::shared_ptr<database::IAuditRepository> original;

        core::Status append(database::ITransaction&, const database::AuditRecord&,
                            const core::CallContext&) override {
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplied);
        }

        core::Result<std::vector<database::AuditRecord>> list(
            database::ITransaction& transaction, std::size_t offset, std::size_t limit,
            const core::CallContext& context) override {
            return original->list(transaction, offset, limit, context);
        }

        core::Status prune(database::ITransaction& transaction, const std::string& before,
                           const core::CallContext& context) override {
            return original->prune(transaction, before, context);
        }
    };

    auto reject = std::make_shared<RejectAudit>();
    reject->original = db.audit;
    auto broken = db;
    broken.audit = reject;
    auto workspace = (std::filesystem::temp_directory_path() / random_token()).u8string();
    DeviceService broken_service(broken, workspace), device_service(db, workspace);
    auto actor = sessions.get(token, call()).principal;
    Json update = {{"device_id", "station"},
                   {"expected_config_revision", "0"},
                   {"fields", {{"name", "Updated"}}}};
    EXPECT_THROW(broken_service.execute("/api/devices/update", update, actor, call().context),
                 ApiError);
    auto detail = device_service.execute("/api/devices/detail", {{"device_id", "station"}}, actor,
                                         call().context);
    EXPECT_EQ(detail.data.at("name"), "Original");
    EXPECT_EQ(detail.meta.at("config_revision"), "0");
    auto updated = device_service.execute("/api/devices/update", update, actor, call().context);
    EXPECT_EQ(updated.meta.at("config_revision"), "1");
    // 两个管理员并发停用对方，只能有一个成功；另一个必须重读已撤销的身份。
    auto admin = execute("/api/users/create", {{"username", "supervisor2"},
                                               {"display_name", "第二管理员"},
                                               {"role_code", "admin"},
                                               {"status", "enabled"},
                                               {"scope_mode", "all"},
                                               {"scopes", Json::array()},
                                               {"temporary_password", "InitialSafe!2026"}});
    auto admin_login =
        sessions.login({{"username", "supervisor2"}, {"password", "InitialSafe!2026"}}, call());
    sessions.execute("/api/auth/password/complete", admin_login.first,
                     {{"old_password", "InitialSafe!2026"}, {"new_password", "ChangedSafe!2026"}},
                     call());
    auto second_admin_token =
        sessions.login({{"username", "supervisor2"}, {"password", "ChangedSafe!2026"}}, call())
            .first;
    auto first_admin = sessions.get(token, call()).principal.subject_id;
    auto first_detail = execute("/api/users/detail", {{"user_id", first_admin}});
    auto second_detail = execute("/api/users/detail", {{"user_id", admin.at("user_id")}});
    std::promise<void> start;
    auto started = start.get_future().share();
    auto disable = [&](const std::string& own_token, const Json& target) {
        started.wait();
        try {
            users.execute("/api/users/set-status", own_token,
                          {{"user_id", target.at("user_id")},
                           {"expected_edit_token", target.at("edit_token")},
                           {"status", "disabled"}},
                          call());
            return 200;
        } catch (const ApiError& e) {
            return e.status;
        }
    };
    auto disable_first =
        std::async(std::launch::async, [&] { return disable(second_admin_token, first_detail); });
    auto disable_second =
        std::async(std::launch::async, [&] { return disable(token, second_detail); });
    start.set_value();
    auto one = disable_first.get(), two = disable_second.get();
    EXPECT_TRUE((one == 200 && two == 401) || (one == 401 && two == 200));
    auto final_tx = take(db.database->begin(call().context));
    EXPECT_EQ(take(db.users->count_users(*final_tx, true, call().context)), 1);
    take(final_tx->rollback(call().context));
}
}  // namespace ems::web
