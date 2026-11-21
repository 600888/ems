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
#ifdef EMS_TEST_ZEROMQ
#include <common/messaging/zmq_change_channel.h>
#endif
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
    std::shared_ptr<rtdb::CommandClient> realtime;
    std::unique_ptr<rtdb::RealtimeClient> realtime_owner;
    std::string realtime_config;

    static auto context() { return core::make_call_context(std::chrono::seconds(5)); }

    void sql(const std::string& statement) {
        char* error = nullptr;
        const auto rc = sqlite3_exec(raw, statement.c_str(), nullptr, nullptr, &error);
        EXPECT_EQ(rc, SQLITE_OK) << (error ? error : "");
        sqlite3_free(error);
    }

    void SetUp() override {
        ConfigurationDatabase::SetUp();
#ifdef EMS_TEST_SHM
        realtime_config = (dir / "realtime.json").u8string();
        const auto segment =
            "command_test_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::ofstream(std::filesystem::u8path(realtime_config)) << nlohmann::json{
            {"realtime",
             {{"backend",
               {{"type", "shm"},
                {"options",
                 {{"segment_name", segment},
                  {"capacity", 1024},
                  {"segment_bytes", 1048576}}}}}}}}.dump();
        auto owner = rtdb::RealtimeClient::initialize(realtime_config, context());
        ASSERT_TRUE(core::is_ok(owner)) << core::error_of(owner).message;
        realtime_owner = std::move(std::get<0>(owner));
        auto client = rtdb::CommandClient::open(
            realtime_config, {rtdb::ScopeKind::Station, "station"}, "", context());
        ASSERT_TRUE(core::is_ok(client));
        realtime = core::value_of(client);
#endif
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
        realtime.reset();
        realtime_owner.reset();
        if (!realtime_config.empty()) rtdb::RealtimeClient::cleanup(realtime_config, context());
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

#ifdef EMS_TEST_SHM
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

core::Result<rtdb::CommandSubmission> submit_command(rtdb::CommandClient& client,
                                                     const std::string& id, const Json& payload) {
    return client.submitCommand(
        {id, payload.at("point_id").get<std::string>(), payload.at("value").get<double>(),
         payload.at("config_revision").get<std::int64_t>(),
         payload.at("expires_at_ms").get<std::int64_t>()},
        core::make_call_context(2s));
}

std::string command_status(rtdb::CommandClient& client, const std::string& id) {
    auto result = client.readCommand(id, core::make_call_context(2s));
    return core::is_ok(result) ? core::value_of(result).status : "";
}

bool wait_status(collect::CommandService& service, rtdb::CommandClient& client,
                 const std::string& id, const char* expected) {
    const auto until = std::chrono::steady_clock::now() + 3s;
    do {
        if (!service.poll()) return false;
        if (command_status(client, id) == expected) return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < until);
    return false;
}

TEST_F(ConfigurationStore, CommandScalesWakesDeduplicatesAndPersistsQuery) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station", realtime);
    ASSERT_TRUE(service.recover());
    service.set_accepting(true);
    const auto payload = submit_payload();
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(core::is_ok(submit_command(*realtime, "cmd-1", payload)));
    ASSERT_TRUE(wait_status(service, *realtime, "cmd-1", "Acked"));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    EXPECT_DOUBLE_EQ(router.state->raw, 23.5);
    EXPECT_TRUE(core::value_of(submit_command(*realtime, "cmd-1", payload)).duplicate);
    auto different = payload;
    different["value"] = 49;
    EXPECT_EQ(core::error_of(submit_command(*realtime, "cmd-1", different)).code,
              core::ErrorCode::Conflict);
    auto peer = rtdb::CommandClient::open(realtime_config, {rtdb::ScopeKind::Station, "station"},
                                          "", context());
    ASSERT_TRUE(core::is_ok(peer));
    EXPECT_EQ(command_status(*core::value_of(peer), "cmd-1"), "Acked");
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, CommandRunsBetweenReadTransactions) {
    ControlRouter router(false, true, 2);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "insert", submit_payload())));
    EXPECT_EQ(router.state->writes, 0);
    ASSERT_TRUE(service.poll());
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "insert", "Acked"));
    EXPECT_EQ(router.state->writes, 1);
    EXPECT_EQ(router.state->write_read_count, 1);
}

TEST_F(ConfigurationStore, QueuedCommandExpiresAndNeverWrites) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    EXPECT_TRUE(
        core::is_ok(submit_command(*realtime, "expires", submit_payload(command_time() + 60))));
    ASSERT_TRUE(service.poll());
    std::this_thread::sleep_for(100ms);
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "expires", "Expired"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, ControlInsertsAfterCurrentReadAndStopCancelsQueue) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "stopped", submit_payload())));
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(router.state->writes, 0);
    router.worker->stop();
    router.release();
    router.worker->join();
    EXPECT_EQ(command_status(*realtime, "stopped"), "Rejected");
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, RevisionChangeRejectsQueuedCommand) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "old-route", submit_payload())));
    ASSERT_TRUE(service.poll());
    router.revision = 8;
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "old-route", "Rejected"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, FailedExecutionRegistrationNeverSends) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "db-failed", submit_payload())));
    sql("CREATE TRIGGER reject_send BEFORE UPDATE ON ops_command WHEN NEW.status='Executing' "
        "BEGIN SELECT RAISE(ABORT,'registration unavailable'); END");
    router.release();
    ASSERT_TRUE(service.poll());
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(router.state->writes, 0);
    EXPECT_EQ(command_status(*realtime, "db-failed"), "Queued");
    sql("DROP TRIGGER reject_send");
    ASSERT_TRUE(wait_status(service, *realtime, "db-failed", "Acked"));
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, LostWriteReplyIsUnknownAndOnlyFreshFeedbackCanVerify) {
    ControlRouter router(true);
    router.state->lost_reply = true;
    router.state->stale = true;
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "lost", payload)));
    ASSERT_TRUE(wait_status(service, *realtime, "lost", "Unknown"));
    EXPECT_EQ(router.state->writes, 1);
    EXPECT_TRUE(core::value_of(submit_command(*realtime, "lost", payload)).duplicate);
    for (int i = 0; i < 5; ++i) {
        service.poll();
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_EQ(command_status(*realtime, "lost"), "Unknown");
    EXPECT_EQ(router.state->writes, 1);
    router.state->stale = false;
    ASSERT_TRUE(wait_status(service, *realtime, "lost", "Succeeded"));
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, RestartNeverReplaysExecutingCommand) {
    ControlRouter router(true, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "restarted", payload)));
    ASSERT_TRUE(service.poll());
    sql("UPDATE ops_command SET status='Executing',sent_at_ms=1 WHERE command_id='restarted'");
    ASSERT_TRUE(service.recover());
    EXPECT_EQ(command_status(*realtime, "restarted"), "Unknown");
    router.state->raw = 23.5;
    router.release();
    service.set_accepting(true);
    ASSERT_TRUE(wait_status(service, *realtime, "restarted", "Succeeded"));
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, InvalidValuesAreRejectedBeforeWrite) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    auto payload = submit_payload();
    payload["value"] = 1000;
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "range", payload)));
    ASSERT_TRUE(service.poll());
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "range", "Rejected"));
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
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "commit-lost", payload)));
    SqliteTestConnection::unknown_execution_commit = true;
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "commit-lost", "Unknown"));
    SqliteTestConnection::unknown_execution_commit = false;
    EXPECT_EQ(router.state->writes, 0);
    EXPECT_TRUE(core::value_of(submit_command(*realtime, "commit-lost", payload)).duplicate);
    EXPECT_TRUE(service.poll());
    EXPECT_EQ(router.state->writes, 0);
}

TEST_F(ConfigurationStore, RealtimeSubmissionSurvivesLostNotificationAndKeepsIndependentCommands) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    ASSERT_TRUE(service.recover());
    service.set_accepting(true);
    auto peer = rtdb::CommandClient::open(realtime_config, {rtdb::ScopeKind::Station, "station"},
                                          "", context());
    ASSERT_TRUE(core::is_ok(peer));
    auto producer = core::value_of(peer);
    const auto deadline = command_time() + 5000;
    const rtdb::CommandRequest first{"independent-a", "setpoint", 48, 7, deadline};
    const rtdb::CommandRequest second{"independent-b", "setpoint", 50, 7, deadline};
    auto submitted = producer->submitCommand(first, context());
    ASSERT_TRUE(core::is_ok(submitted));
    EXPECT_FALSE(core::value_of(submitted).notification_queued);
    EXPECT_EQ(core::value_of(realtime->readCommand(first.command_id, context())).status, "Pending");
    EXPECT_EQ(router.state->writes, 0);
    ASSERT_TRUE(core::is_ok(producer->submitCommand(second, context())));
    EXPECT_EQ(core::value_of(producer->submitCommand(first, context())).duplicate, true);
    auto changed = first;
    changed.value = 99;
    EXPECT_EQ(core::error_of(producer->submitCommand(changed, context())).code,
              core::ErrorCode::Conflict);
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(command_status(*realtime, first.command_id), "Queued");
    EXPECT_EQ(command_status(*realtime, second.command_id), "Queued");
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, first.command_id, "Acked"));
    ASSERT_TRUE(wait_status(service, *realtime, second.command_id, "Acked"));
    EXPECT_EQ(router.state->writes, 2);
    EXPECT_EQ(core::value_of(producer->readCommand(first.command_id, context())).status, "Acked");
    EXPECT_EQ(core::value_of(producer->readCommand(second.command_id, context())).request.value,
              50);
}

TEST_F(ConfigurationStore, ConcurrentRealtimeClaimsHaveOneWinner) {
    const rtdb::CommandRequest request{"claim-once", "setpoint", 48, 7, command_time() + 5000};
    auto submitted = realtime->submitCommand(request, context());
    ASSERT_TRUE(core::is_ok(submitted));
    auto record = core::value_of(submitted).record;
    record.status = "Executing";
    std::atomic<int> wins{0};
    std::thread one([&] {
        if (core::is_ok(realtime->updateCommand(record, context()))) ++wins;
    });
    std::thread two([&] {
        if (core::is_ok(realtime->updateCommand(record, context()))) ++wins;
    });
    one.join();
    two.join();
    EXPECT_EQ(wins, 1);
}

#ifdef EMS_TEST_ZEROMQ
TEST_F(ConfigurationStore, MultipleProducersNotifyAfterRealtimeCommit) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station", realtime);
    ASSERT_TRUE(service.recover());
    service.set_accepting(true);
    messaging::ZmqChangeConfig config;
    config.endpoint = "tcp://127.0.0.1:*";
    config.publisher_bind = false;
    messaging::ZmqChangeSubscriber subscriber;
    std::atomic<int> received{0};
    ASSERT_TRUE(subscriber.start(config, {"command.pending"}, [&](auto&, const auto& id) {
        auto current = realtime->readCommand(id, context());
        EXPECT_TRUE(core::is_ok(current));
        if (core::is_ok(current)) {
            service.notify(id);
            received.fetch_or(id == "notify-one" ? 1 : 2);
        }
    }));
    auto one = rtdb::CommandClient::open(realtime_config, {rtdb::ScopeKind::Station, "station"},
                                         subscriber.bound_endpoint(), context());
    auto two = rtdb::CommandClient::open(realtime_config, {rtdb::ScopeKind::Station, "station"},
                                         subscriber.bound_endpoint(), context());
    ASSERT_TRUE(core::is_ok(one));
    ASSERT_TRUE(core::is_ok(two));
    const auto deadline = command_time() + 5000;
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (received != 3 && std::chrono::steady_clock::now() < until) {
        ASSERT_TRUE(core::is_ok(core::value_of(one)->submitCommand(
            {"notify-one", "setpoint", 48, 7, deadline}, context())));
        ASSERT_TRUE(core::is_ok(core::value_of(two)->submitCommand(
            {"notify-two", "setpoint", 50, 7, deadline}, context())));
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(received, 3);
    EXPECT_EQ(router.state->writes, 0);
    ASSERT_TRUE(wait_status(service, *realtime, "notify-one", "Acked"));
    ASSERT_TRUE(wait_status(service, *realtime, "notify-two", "Acked"));
    EXPECT_EQ(router.state->writes, 2);
    subscriber.stop();
}
#endif

TEST_F(ConfigurationStore, RecoveryRepairsStaleRealtimeTerminalWithoutResending) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "repair-terminal", submit_payload())));
    ASSERT_TRUE(wait_status(service, *realtime, "repair-terminal", "Acked"));
    auto current = realtime->readCommand("repair-terminal", context());
    ASSERT_TRUE(core::is_ok(current));
    auto stale = core::value_of(current);
    stale.status = "Executing";
    stale.reason = "simulated_crash_before_realtime_update";
    ASSERT_TRUE(core::is_ok(realtime->updateCommand(stale, context())));
    ASSERT_TRUE(service.recover());
    EXPECT_EQ(command_status(*realtime, "repair-terminal"), "Acked");
    EXPECT_EQ(router.state->writes, 1);
}

TEST_F(ConfigurationStore, DurableDedupSurvivesRealtimeRecordCleanupAndPayloadConflict) {
    ControlRouter router;
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    const auto payload = submit_payload();
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "durable-id", payload)));
    ASSERT_TRUE(wait_status(service, *realtime, "durable-id", "Acked"));
    auto opened = rtdb::RealtimeClient::open(realtime_config, context());
    ASSERT_TRUE(core::is_ok(opened));
    auto raw_client = std::move(std::get<0>(opened));
    const rtdb::RecordKey key{
        {rtdb::ScopeKind::Station, "station"}, rtdb::RecordKind::CommandState, {}, "durable-id"};
    auto cleanup_record = [&] {
        auto stored = raw_client->read(key, context());
        return core::is_ok(stored) &&
               core::is_ok(raw_client->erase({key, core::value_of(stored).revision}, context()));
    };
    ASSERT_TRUE(cleanup_record());
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "durable-id", payload)));
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(command_status(*realtime, "durable-id"), "Acked");
    EXPECT_EQ(router.state->writes, 1);
    ASSERT_TRUE(cleanup_record());
    auto conflicting = payload;
    conflicting["value"] = 99;
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "durable-id", conflicting)));
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(command_status(*realtime, "durable-id"), "Rejected");
    EXPECT_EQ(router.state->writes, 1);
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "unrelated-id", payload)));
    ASSERT_TRUE(wait_status(service, *realtime, "unrelated-id", "Acked"));
    EXPECT_EQ(router.state->writes, 2);
}

TEST_F(ConfigurationStore, NotificationChecksCommandBeforeItsRotatingScanPage) {
    ControlRouter router(false, true);
    collect::CommandService service(database, commands, router, "station", realtime);
    service.set_accepting(true);
    for (int i = 0; i < 128; ++i) {
        rtdb::CommandRecord terminal;
        terminal.request = {"a" + std::to_string(i), "setpoint", 0, 7, command_time() + 5000};
        terminal.updated_at_ms = command_time();
        terminal.status = "Rejected";
        ASSERT_TRUE(core::is_ok(realtime->updateCommand(terminal, context())));
    }
    ASSERT_TRUE(core::is_ok(submit_command(*realtime, "z-notified", submit_payload())));
    service.notify("z-notified");
    ASSERT_TRUE(service.poll());
    EXPECT_EQ(command_status(*realtime, "z-notified"), "Queued");
    router.release();
    ASSERT_TRUE(wait_status(service, *realtime, "z-notified", "Acked"));
    EXPECT_EQ(router.state->writes, 1);
}
#endif

#endif
}  // namespace
}  // namespace ems::database
