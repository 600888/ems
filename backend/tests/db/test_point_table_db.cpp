#include <common/database/mysql_database.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include "net/para_loader/cfg_loader.h"
#ifdef EMS_TEST_SQLITE
#include <sqlite3.h>

#include <ormpp/sqlite.hpp>

#include "common/db/configuration/orm_device_config_repository.h"
#include "common/db/runtime/transaction_database.h"
#endif

namespace ems::database {
namespace {
class ConfigurationDatabase : public ::testing::Test {
   protected:
    std::filesystem::path dir;

    void SetUp() override {
        dir = std::filesystem::temp_directory_path() /
              ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(dir);
    }

    void TearDown() override { std::filesystem::remove_all(dir); }
};

class MysqlEnvironment {
   public:
    MysqlEnvironment() {
        for (const auto* key : keys_) {
            const char* value = std::getenv(key);
            previous_.push_back(value ? std::optional<std::string>(value) : std::nullopt);
            set(key, nullptr);
        }
    }

    ~MysqlEnvironment() {
        for (std::size_t i = 0; i < keys_.size(); ++i)
            set(keys_[i], previous_[i] ? previous_[i]->c_str() : nullptr);
    }

    static void set(const char* key, const char* value) {
#ifdef _WIN32
        _putenv_s(key, value ? value : "");
#else
        if (value)
            setenv(key, value, 1);
        else
            unsetenv(key);
#endif
    }

   private:
    const std::vector<const char*> keys_{"EMS_DATABASE_CONFIG",
                                         "EMS_MYSQL_HOST",
                                         "EMS_MYSQL_PORT",
                                         "EMS_MYSQL_USER",
                                         "EMS_MYSQL_PASSWORD",
                                         "EMS_MYSQL_DATABASE",
                                         "EMS_MYSQL_MAX_CONNECTIONS",
                                         "EMS_MYSQL_ACQUIRE_TIMEOUT_MS",
                                         "EMS_MYSQL_IO_TIMEOUT_SECONDS"};
    std::vector<std::optional<std::string>> previous_;
};

TEST_F(ConfigurationDatabase, MysqlConfigurationIsStrictAndEnvironmentOverridesFile) {
    MysqlEnvironment environment;
    const auto file = dir / "database.json";
    {
        std::ofstream output(file);
        output
            << R"({"user":"file_user","database":"configuration","port":3307,"max_connections":2})";
    }
    MysqlEnvironment::set("EMS_MYSQL_USER", "environment_user");
    auto options = load_mysql_options(file.u8string());
    ASSERT_TRUE(core::is_ok(options));
    EXPECT_EQ(core::value_of(options).user, "environment_user");
    EXPECT_EQ(core::value_of(options).port, 3307);
    EXPECT_EQ(core::value_of(options).max_connections, 2u);
    MysqlEnvironment::set("EMS_MYSQL_PORT", "3306garbage");
    EXPECT_FALSE(core::is_ok(load_mysql_options(file.u8string())));
    MysqlEnvironment::set("EMS_MYSQL_PORT", "0");
    EXPECT_FALSE(core::is_ok(load_mysql_options(file.u8string())));
}

TEST_F(ConfigurationDatabase, InvalidConfigurationDoesNotSilentlyFallBack) {
    MysqlEnvironment environment;
    EXPECT_FALSE(core::is_ok(load_mysql_options((dir / "absent.json").u8string())));
    const auto file = dir / "database.json";
    for (const auto* contents :
         {"{broken", "[]", R"({"user":"test","database":"test","port":3306.5})",
          R"({"user":"test","database":"test","max_connections":-1})"}) {
        {
            std::ofstream output(file);
            output << contents;
        }
        auto options = load_mysql_options(file.u8string());
        ASSERT_FALSE(core::is_ok(options));
        EXPECT_EQ(core::error_of(options).code, core::ErrorCode::InvalidArgument);
    }
}

#ifdef EMS_TEST_SQLITE
class SqliteTestConnection final : public detail::Connection {
   public:
    ormpp::sqlite orm;
    inline static bool unknown_commit = false;

    core::Status prepare(const core::CallContext& ctx) {
        if (core::has_expired(ctx))
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "test deadline");
        return core::ok_status();
    }

    core::Status begin(const core::CallContext&) override { return status(orm.begin()); }

    core::Status commit(const core::CallContext&) override {
        auto result = status(orm.commit());
        if (unknown_commit && core::is_ok(result))
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                    "lost commit reply");
        return result;
    }

    core::Status rollback(const core::CallContext&) override { return status(orm.rollback()); }

    static core::Status status(bool ok) {
        return ok ? core::ok_status()
                  : core::Status{core::make_error(core::ErrorCode::Unavailable,
                                                  core::WriteEffect::Unknown,
                                                  "test database failure")};
    }
};

class ConfigurationStore : public ConfigurationDatabase {
   protected:
    std::shared_ptr<detail::TransactionDatabase> database;
    std::shared_ptr<IDeviceConfigRepository> repository;
    sqlite3* raw{nullptr};

    static auto context() { return core::make_call_context(std::chrono::seconds(5)); }

    void sql(const std::string& statement) {
        char* error = nullptr;
        const auto rc = sqlite3_exec(raw, statement.c_str(), nullptr, nullptr, &error);
        EXPECT_EQ(rc, SQLITE_OK) << (error ? error : "");
        sqlite3_free(error);
    }

    void SetUp() override {
        ConfigurationDatabase::SetUp();
        const auto file = (dir / "configuration.db").u8string();
        ASSERT_EQ(sqlite3_open(file.c_str(), &raw), SQLITE_OK);
        std::ifstream schema(EMS_CONFIG_SCHEMA_FILE);
        ASSERT_TRUE(schema.good());
        std::ostringstream contents;
        contents << schema.rdbuf();
        sql(contents.str());
        sql("INSERT INTO "
            "cfg_device(id,device_id,parent_device_id,node_kind,code,name,path,enabled,config_"
            "revision) "
            "VALUES (1,'station','','station','station','Station','/',1,3),"
            "(2,'pcs','station','device','pcs','PCS','/pcs',1,0)");
        sql("INSERT INTO "
            "cfg_channel(id,channel_id,code,name,protocol,conn_mode,endpoint,poll_interval_ms,"
            "timeout_ms,retry_times,max_sessions,enabled) "
            "VALUES "
            "(1,'tcp','tcp','Modbus','modbus','tcp_client','127.0.0.1:1502',1000,3000,1,1,1)");
        sql("INSERT INTO "
            "cfg_binding(id,binding_id,device_id,channel_id,device_address,can_collect,can_control,"
            "enabled) "
            "VALUES (1,'bind','pcs','tcp','7',1,1,1)");
        sql("INSERT INTO "
            "cfg_point_yc(id,point_id,owner_device_id,binding_id,code,address_space,address_offset,"
            "bit_offset,register_count,wire_type,byte_order,scale_factor,offset_value,enabled) "
            "VALUES "
            "(1,'stable-voltage','pcs','bind','voltage','holding_register',10,0,2,'f32','ab',0.1,1,"
            "1)");
        database = std::make_shared<detail::TransactionDatabase>(
            detail::TransactionOptions{},
            [file](const core::CallContext&) -> core::Result<detail::ConnectionLease> {
                auto connection = std::make_shared<SqliteTestConnection>();
                if (!connection->orm.connect("", "", "", file, {}, {}))
                    return core::make_error(core::ErrorCode::Unavailable,
                                            core::WriteEffect::NotApplied, "test connect");
                connection->orm.set_enable_transaction(false);
                return detail::ConnectionLease{connection,
                                               [](std::shared_ptr<detail::Connection>, bool) {}};
            });
        repository = std::make_shared<detail::OrmDeviceConfigRepository<SqliteTestConnection>>(
            [this](ITransaction& tx,
                   const core::CallContext& ctx) -> core::Result<SqliteTestConnection*> {
                auto connection = database->connection(tx, ctx);
                if (!core::is_ok(connection)) return core::error_of(connection);
                return static_cast<SqliteTestConnection*>(core::value_of(connection));
            },
            [this](ITransaction& tx) { database->invalidate(tx); },
            [](SqliteTestConnection&) {
                return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplied,
                                        "test SQL failure");
            });
    }

    void TearDown() override {
        repository.reset();
        database.reset();
        sqlite3_close(raw);
        ConfigurationDatabase::TearDown();
    }

    MySqlDatabaseContext config_context() {
        MySqlDatabaseContext result;
        result.database = database;
        result.device_config = repository;
        return result;
    }
};

TEST_F(ConfigurationStore, LoadsWithoutAnyLegacyTablesAndReloadsNewBindingAddress) {
    net::device::CfgLoader loader;
    ASSERT_TRUE(loader.initialize(config_context(), context())) << loader.last_error();
    ASSERT_EQ(loader.channels().size(), 1u);
    EXPECT_EQ(loader.config_revision(), 3);
    ASSERT_EQ(loader.devices(1).size(), 1u);
    EXPECT_EQ(loader.devices(1)[0].ycPoints_[0].point_id, "stable-voltage");
    EXPECT_EQ(loader.devices(1)[0].ycPoints_[0].protocol_code, "7:3:10:4:0");
    sql("UPDATE cfg_binding SET device_address='9'");
    sql("UPDATE cfg_device SET config_revision=4 WHERE node_kind='station'");
    ASSERT_TRUE(loader.reload(context())) << loader.last_error();
    EXPECT_EQ(loader.config_revision(), 4);
    EXPECT_EQ(loader.devices(1)[0].ycPoints_[0].protocol_code, "9:3:10:4:0");
}

TEST_F(ConfigurationStore, MissingNewTableDoesNotDiscardLastValidConfiguration) {
    net::device::CfgLoader loader;
    ASSERT_TRUE(loader.initialize(config_context(), context())) << loader.last_error();
    sql("DROP TABLE cfg_point_yx");
    EXPECT_FALSE(loader.reload(context()));
    EXPECT_EQ(loader.config_revision(), 3);
    EXPECT_EQ(loader.devices(1)[0].ycPoints_[0].point_id, "stable-voltage");
    net::device::CfgLoader fresh;
    EXPECT_FALSE(fresh.initialize(config_context(), context()));
    EXPECT_FALSE(fresh.is_initialized());
}

TEST_F(ConfigurationStore, ExpiredReadIsRejected) {
    auto tx = database->begin(context());
    ASSERT_TRUE(core::is_ok(tx));
    auto handle = std::move(std::get<0>(tx));
    auto expired = core::make_call_context(std::chrono::milliseconds(-1));
    EXPECT_FALSE(core::is_ok(repository->load_configuration(*handle, expired)));
}
#endif
}  // namespace
}  // namespace ems::database
