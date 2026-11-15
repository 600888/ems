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

#include <condition_variable>
#include <nlohmann/json.hpp>
#include <ormpp/sqlite.hpp>
#include <thread>

#include "common/db/collector_commands.h"
#include "common/db/configuration/orm_device_config_repository.h"
#include "common/db/runtime/transaction_database.h"
#include "net/collect/command_service.h"
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
    inline static std::atomic<bool> unknown_execution_commit{false};

    core::Status prepare(const core::CallContext& ctx) {
        if (core::has_expired(ctx))
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "test deadline");
        return core::ok_status();
    }

    core::Status begin(const core::CallContext&) override {
        // SQLite 的 Transaction 行锁能力由写事务提供，读取后再写不能使用 deferred begin。
        return status(orm.execute("BEGIN IMMEDIATE"));
    }

    core::Status commit(const core::CallContext&) override {
        bool execution = false;
        if (unknown_execution_commit) {
            const auto rows = orm.query_s<std::tuple<int>>(
                "SELECT COUNT(*) FROM ops_command WHERE status='Executing'");
            execution = !rows.empty() && std::get<0>(rows.front()) > 0;
            if (execution) unknown_execution_commit = false;
        }
        auto result = status(orm.commit());
        if ((unknown_commit || execution) && core::is_ok(result))
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
    std::shared_ptr<ICollectorCommandRepository> commands;
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
        SqliteTestConnection::unknown_execution_commit = false;
        const auto file = (dir / "configuration.db").u8string();
        ASSERT_EQ(sqlite3_open(file.c_str(), &raw), SQLITE_OK);
        sql("PRAGMA journal_mode=WAL");
        sql("PRAGMA busy_timeout=2000");
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
                connection->orm.query_s<std::tuple<int>>("PRAGMA busy_timeout=2000");
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
        commands = std::make_shared<detail::OrmCollectorCommandRepository<SqliteTestConnection>>(
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
            },
            detail::CommandRowLock::Transaction);
    }

    void TearDown() override {
        commands.reset();
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

using namespace std::chrono_literals;
namespace collect = ems::net::collect;
using Json = nlohmann::json;

std::int64_t command_time() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct ControlState {
    std::atomic<int> reads{0}, writes{0}, write_read_count{0};
    std::atomic<double> raw{0};
    std::atomic<bool> hold_read{false}, lost_reply{false}, stale{false};
    std::mutex mutex;
    std::condition_variable cv;
};

class ControlHandler final : public net::handler::ProtocolHandler {
   public:
    explicit ControlHandler(std::shared_ptr<ControlState> state) : state_(std::move(state)) {}

    bool initialize(const Json&) override { return true; }

    bool connect() override { return true; }

    bool disconnect() override { return true; }

    bool is_connected() const override { return true; }

    int read_batch(const std::vector<std::string>& codes,
                   std::vector<net::handler::PointValue>& values) override {
        ++state_->reads;
        state_->cv.notify_all();
        std::unique_lock lock(state_->mutex);
        state_->cv.wait(lock, [&] { return !state_->hold_read.load(); });
        lock.unlock();
        std::this_thread::sleep_for(2ms);
        values.clear();
        for (const auto& code : codes) {
            net::handler::PointValue value;
            value.code = code;
            value.value = state_->raw;
            value.quality = 0;
            value.timestamp = state_->stale ? 1 : command_time();
            value.details = {{"received_at_ms", value.timestamp}};
            values.push_back(value);
        }
        return static_cast<int>(values.size());
    }

    bool write_value(const std::string&, double value) override {
        ++state_->writes;
        state_->write_read_count = state_->reads.load();
        state_->raw = value;  // 设备已执行；lost_reply 模拟响应丢失。
        return !state_->lost_reply.load();
    }

    std::string last_error() const override { return "simulated_lost_response"; }

    std::string protocol_name() const override { return "ModbusTCP"; }

   private:
    std::shared_ptr<ControlState> state_;
};

class ControlRouter final : public collect::CommandRouter {
   public:
    std::shared_ptr<ControlState> state = std::make_shared<ControlState>();
    collect::PluginManager plugins;
    std::shared_ptr<collect::ChannelThread> worker;
    std::atomic<std::int64_t> revision{7};
    bool running{true};

    explicit ControlRouter(bool feedback = false, bool hold = false, int inputs = 1) {
        state->hold_read = hold;
        rtdb::ChannelConfig config;
        config.id_ = 1;
        config.name_ = "control-test";
        config.protocol_ = ems::model::ProtocolType::ModbusTCP;
        worker = std::make_shared<collect::ChannelThread>(config, nullptr, [state = state](auto&) {
            return std::make_unique<ControlHandler>(state);
        });
        net::device::ConfiguredDevice device;
        device.deviceKey_ = "pcs";
        net::device::PointRoute route{"setpoint", "write", 2, 1, false, "scaled"};
        route.range_min = -100;
        route.range_max = 100;
        route.wire_type = "f32";
        if (feedback)
            route.feedback_json =
                R"([{"point_code":"feedback","criterion":"within_tolerance","tolerance":0.001}])";
        device.ytPoints_.push_back(route);
        net::device::PointRoute input{"readback", "read", 2, 1};
        input.point_code = "feedback";
        device.ycPoints_.push_back(input);
        if (inputs > 1) {
            input.point_id = "readback2";
            input.protocol_code = "read2";
            device.ycPoints_.push_back(input);
        }
        worker->add_device(device);
        worker->set_poll_interval(10000);
        if (!worker->start(plugins)) throw std::runtime_error("fake worker start");
        std::unique_lock lock(state->mutex);
        if (!state->cv.wait_for(lock, 2s, [&] { return state->reads > 0; }))
            throw std::runtime_error("fake worker initial read");
    }

    ~ControlRouter() override {
        release();
        worker->stop();
        worker->join();
    }

    void release() {
        state->hold_read = false;
        state->cv.notify_all();
    }

    std::shared_ptr<collect::ChannelThread> command_channel(
        const std::string& point) const override {
        return running && point == "setpoint" ? worker : nullptr;
    }

    std::int64_t config_revision() const override { return revision; }

    bool is_running() const override { return running; }
};

Json submit_payload(std::int64_t expires = command_time() + 5000) {
    return {{"point_id", "setpoint"},
            {"value", 48.0},
            {"config_revision", 7},
            {"expires_at_ms", expires}};
}

std::string command_status(collect::CommandService& service, const std::string& id) {
    auto result = service.handle({id, R"({"op":"query"})"});
    if (result.code != messaging::ZmqCommandReplyCode::Result) return "";
    return Json::parse(result.reason).at("status").get<std::string>();
}

bool wait_status(collect::CommandService& service, const std::string& id, const char* expected) {
    const auto until = std::chrono::steady_clock::now() + 3s;
    do {
        if (!service.poll()) return false;
        if (command_status(service, id) == expected) return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < until);
    return false;
}

TEST_F(ConfigurationStore, CommandScalesWakesDeduplicatesAndPersistsQuery) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station");
    ASSERT_TRUE(service.recover());
    service.set_accepting(true);
    const auto payload = submit_payload();
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(service.handle({"cmd-1", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(wait_status(service, "cmd-1", "Acked"));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    EXPECT_DOUBLE_EQ(router.state->raw, 23.5);
    EXPECT_EQ(service.handle({"cmd-1", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Duplicate);
    auto different = payload;
    different["value"] = 49;
    EXPECT_EQ(service.handle({"cmd-1", different.dump()}).code,
              messaging::ZmqCommandReplyCode::Rejected);
    collect::CommandService reader(database, commands, router, "station");
    EXPECT_EQ(command_status(reader, "cmd-1"), "Acked");
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, CommandRunsBetweenReadTransactions) {
    ControlRouter router(false, true, 2);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    ASSERT_EQ(service.handle({"insert", submit_payload().dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    EXPECT_EQ(router.state->writes, 0);
    router.release();
    ASSERT_TRUE(wait_status(service, "insert", "Acked"));
    EXPECT_EQ(router.state->writes, 1);
    EXPECT_EQ(router.state->write_read_count, 1);
}

TEST_F(ConfigurationStore, QueuedCommandExpiresAndNeverWrites) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    EXPECT_EQ(service.handle({"expires", submit_payload(command_time() + 60).dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(service.poll());
    std::this_thread::sleep_for(100ms);
    router.release();
    ASSERT_TRUE(wait_status(service, "expires", "Expired"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, ControlInsertsAfterCurrentReadAndStopCancelsQueue) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    ASSERT_EQ(service.handle({"stopped", submit_payload().dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(router.state->writes, 0);
    router.worker->stop();
    router.release();
    router.worker->join();
    EXPECT_EQ(command_status(service, "stopped"), "Rejected");
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, RevisionChangeRejectsQueuedCommand) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    ASSERT_EQ(service.handle({"old-route", submit_payload().dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(service.poll());
    router.revision = 8;
    router.release();
    ASSERT_TRUE(wait_status(service, "old-route", "Rejected"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, FailedExecutionRegistrationNeverSends) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    ASSERT_EQ(service.handle({"db-failed", submit_payload().dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    sql("CREATE TRIGGER reject_send BEFORE UPDATE ON ops_command WHEN NEW.status='Executing' "
        "BEGIN SELECT RAISE(ABORT,'registration unavailable'); END");
    router.release();
    ASSERT_TRUE(service.poll());
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(router.state->writes, 0);
    EXPECT_EQ(command_status(service, "db-failed"), "Queued");
    sql("DROP TRIGGER reject_send");
    ASSERT_TRUE(wait_status(service, "db-failed", "Acked"));
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, LostWriteReplyIsUnknownAndOnlyFreshFeedbackCanVerify) {
    ControlRouter router(true);
    router.state->lost_reply = true;
    router.state->stale = true;
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_EQ(service.handle({"lost", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(wait_status(service, "lost", "Unknown"));
    EXPECT_EQ(router.state->writes, 1);
    EXPECT_EQ(service.handle({"lost", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Duplicate);
    for (int i = 0; i < 5; ++i) {
        service.poll();
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(command_status(service, "lost"), "Unknown");
    EXPECT_EQ(router.state->writes, 1);
    router.state->stale = false;
    ASSERT_TRUE(wait_status(service, "lost", "Succeeded"));
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, RestartNeverReplaysExecutingCommand) {
    ControlRouter router(true, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_EQ(service.handle({"restarted", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    sql("UPDATE ops_command SET status='Executing',sent_at_ms=1 WHERE command_id='restarted'");
    ASSERT_TRUE(service.recover());
    EXPECT_EQ(command_status(service, "restarted"), "Unknown");
    router.state->raw = 23.5;
    router.release();
    service.set_accepting(true);
    ASSERT_TRUE(wait_status(service, "restarted", "Succeeded"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, InvalidValuesAreRejectedBeforeWrite) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    auto payload = submit_payload();
    payload["value"] = 1000;
    ASSERT_EQ(service.handle({"range", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(service.poll());
    router.release();
    ASSERT_TRUE(wait_status(service, "range", "Rejected"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, CommandQueueIsBoundedAndEveryCancellationCompletes) {
    ControlRouter router(false, true);
    std::atomic<int> cancelled{0};
    collect::PointCommand command{"bounded", "setpoint", 48, command_time() + 5000, 7};
    for (int i = 0; i < 128; ++i)
        ASSERT_TRUE(router.worker->enqueue_command(
            command, [] { return true; },
            [&](const auto& result) {
                if (result.status == "Rejected") ++cancelled;
            }));
    EXPECT_FALSE(router.worker->enqueue_command(command, [] { return true; }, [](const auto&) {}));
    router.worker->stop();
    router.release();
    router.worker->join();
    EXPECT_EQ(cancelled, 128);
    EXPECT_EQ(router.state->writes, 0);
    EXPECT_FALSE(router.worker->post_command("setpoint", 1).get());
}

TEST_F(ConfigurationStore, UnknownExecutionCommitNeverStartsPhysicalWrite) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station");
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_EQ(service.handle({"commit-lost", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    SqliteTestConnection::unknown_execution_commit = true;
    router.release();
    ASSERT_TRUE(wait_status(service, "commit-lost", "Unknown"));
    SqliteTestConnection::unknown_execution_commit = false;
    EXPECT_EQ(router.state->writes, 0);
    EXPECT_EQ(service.handle({"commit-lost", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Duplicate);
    EXPECT_TRUE(service.poll());
    EXPECT_EQ(router.state->writes, 0);
}

#ifdef EMS_TEST_ZEROMQ
TEST_F(ConfigurationStore, ZmqSubmitAndQueryDriveDurableCommand) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station");
    ASSERT_TRUE(service.recover());
    service.set_accepting(true);
    messaging::ZmqCommandConfig config;
    config.endpoint = "tcp://127.0.0.1:*";
    messaging::ZmqCommandServer server;
    ASSERT_TRUE(server.start(config, [&](const auto& request) { return service.handle(request); }));
    config.endpoint = server.bound_endpoint();
    messaging::ZmqCommandClient client(config);
    const auto payload = submit_payload();
    EXPECT_EQ(client.send({"zmq-command", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Accepted);
    ASSERT_TRUE(wait_status(service, "zmq-command", "Acked"));
    const auto result = client.send({"zmq-command", R"({"op":"query"})"});
    EXPECT_EQ(result.code, messaging::ZmqCommandReplyCode::Result);
    EXPECT_EQ(Json::parse(result.reason)["status"], "Acked");
    EXPECT_EQ(client.send({"zmq-command", payload.dump()}).code,
              messaging::ZmqCommandReplyCode::Duplicate);
    EXPECT_EQ(router.state->writes, 1);
}
#endif

#endif
}  // namespace
}  // namespace ems::database
