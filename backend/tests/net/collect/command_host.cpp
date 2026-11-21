#include "command_host.h"

#include <sqlite3.h>

#include <filesystem>
#include <ormpp/sqlite.hpp>
#include <thread>

#include "common/db/collector_commands.h"
#include "common/db/runtime/transaction_database.h"
#include "net/collect/command_service.h"
using namespace ems;

namespace {
core::CallContext context() { return core::make_call_context(std::chrono::seconds(3)); }

class Connection : public database::detail::Connection {
   public:
    ormpp::sqlite orm;

    core::Status prepare(const core::CallContext&) { return core::ok_status(); }

    core::Status begin(const core::CallContext&) override {
        return status(orm.execute("BEGIN IMMEDIATE"));
    }

    core::Status commit(const core::CallContext&) override { return status(orm.commit()); }

    core::Status rollback(const core::CallContext&) override { return status(orm.rollback()); }

    static core::Status status(bool ok) {
        return ok ? core::ok_status()
                  : core::Status{core::make_error(core::ErrorCode::Unavailable,
                                                  core::WriteEffect::Unknown, "test SQL failure")};
    }
};
}  // namespace

struct CommandHost::Impl {
    std::string file;
    std::shared_ptr<database::detail::TransactionDatabase> database;
    std::shared_ptr<database::ICollectorCommandRepository> repository;
    std::shared_ptr<rtdb::CommandClient> realtime;
    std::unique_ptr<net::collect::CommandService> service;
    net::collect::CollectEngine& engine;
    int counter{0};

    explicit Impl(net::collect::CollectEngine& value) : engine(value) {}
};

CommandHost::CommandHost(net::collect::CollectEngine& engine, const std::string& config,
                         const std::string& scope)
    : impl_(std::make_unique<Impl>(engine)) {
    auto& state = *impl_;
    state.file = config + ".commands.sqlite";
    sqlite3* db = nullptr;
    if (sqlite3_open(state.file.c_str(), &db) != SQLITE_OK)
        throw std::runtime_error("test command database");
    const char* schema =
        "CREATE TABLE ops_command(id INTEGER PRIMARY KEY AUTOINCREMENT, command_id TEXT NOT NULL, "
        "scope_code TEXT NOT NULL, point_id TEXT NOT NULL, value REAL NOT NULL, config_revision "
        "INTEGER NOT NULL, expires_at_ms INTEGER NOT NULL, payload_json TEXT NOT NULL, status TEXT "
        "NOT NULL, reason TEXT NOT NULL, sent_at_ms INTEGER NOT NULL, updated_at_ms INTEGER NOT "
        "NULL, UNIQUE(scope_code,command_id));";
    const int created = sqlite3_exec(db, schema, nullptr, nullptr, nullptr);
    sqlite3_close(db);
    if (created != SQLITE_OK) throw std::runtime_error("test command schema");
    state.database = std::make_shared<database::detail::TransactionDatabase>(
        database::detail::TransactionOptions{},
        [file = state.file](const auto&) -> core::Result<database::detail::ConnectionLease> {
            auto connection = std::make_shared<Connection>();
            if (!connection->orm.connect("", "", "", file, {}, {}))
                return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplied,
                                        "test command connection");
            connection->orm.set_enable_transaction(false);
            connection->orm.query_s<std::tuple<int>>("PRAGMA busy_timeout=2000");
            return database::detail::ConnectionLease{connection, [](auto, bool) {}};
        });
    auto acquire = [db = state.database](database::ITransaction& tx,
                                         const auto& ctx) -> core::Result<Connection*> {
        auto result = db->connection(tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        return static_cast<Connection*>(core::value_of(result));
    };
    state.repository =
        std::make_shared<database::detail::OrmCollectorCommandRepository<Connection>>(
            acquire, [db = state.database](auto& tx) { db->invalidate(tx); },
            [](Connection&) {
                return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplied,
                                        "test command SQL");
            },
            database::detail::CommandRowLock::Transaction);
    auto opened =
        rtdb::CommandClient::open(config, {rtdb::ScopeKind::Station, scope}, "", context());
    if (!core::is_ok(opened)) throw std::runtime_error(core::error_of(opened).message);
    state.realtime = core::value_of(opened);
    state.service = std::make_unique<net::collect::CommandService>(state.database, state.repository,
                                                                   engine, scope, state.realtime);
    if (!state.service->recover()) throw std::runtime_error("test command recovery");
    state.service->set_accepting(true);
}

CommandHost::~CommandHost() {
    impl_->service.reset();
    impl_->repository.reset();
    impl_->database.reset();
    std::error_code error;
    std::filesystem::remove(std::filesystem::u8path(impl_->file), error);
}

net::collect::PointCommandResult CommandHost::execute(const std::string& point, double value,
                                                      int lifetime) {
    const auto now = core::now_utc_micros() / 1000;
    const auto id = "integration" + std::to_string(++impl_->counter);
    auto result = impl_->realtime->submitCommand(
        {id, point, value, impl_->engine.config_revision(), now + lifetime}, context());
    if (!core::is_ok(result)) throw std::runtime_error(core::error_of(result).message);
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(lifetime + 1000);
    do {
        if (!impl_->service->poll()) throw std::runtime_error("test command poll");
        auto state = impl_->realtime->readCommand(id, context());
        if (!core::is_ok(state)) throw std::runtime_error(core::error_of(state).message);
        const auto& record = core::value_of(state);
        if (record.status != "Pending" && record.status != "Queued" && record.status != "Executing")
            return {record.status, record.reason, record.sent_at_ms};
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("test command timeout");
}
