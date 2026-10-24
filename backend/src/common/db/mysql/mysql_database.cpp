#include <common/database/mysql_database.h>
#include <mysql.h>

#include <algorithm>
#include <array>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <limits>
#include <mutex>
#include <optional>
#include <ormpp/connection_pool.hpp>
#include <ormpp/mysql.hpp>
#include <string_view>
#include <utility>

#include "../configuration/orm_device_config_repository.h"
#include "../configuration/orm_device_management_store.h"
#include "../configuration/orm_repository.h"
#include "../runtime/transaction_database.h"

namespace ems::database::mysql_rows {

struct history_point_version {
    std::string scope_kind;
    std::string scope_id;
    std::uint64_t config_version{0};
    std::optional<std::string> ready_at;
};

YLT_REFL(history_point_version, scope_kind, scope_id, config_version, ready_at);

struct history_point_snapshot {
    std::string scope_kind;
    std::string scope_id;
    std::uint64_t config_version{0};
    std::string point_id;
    std::string owner_device_id;
    std::string point_code;
    std::string display_name;
    std::optional<std::string> unit;
    std::string value_type;
    bool enabled{false};
};

YLT_REFL(history_point_snapshot, scope_kind, scope_id, config_version, point_id, owner_device_id,
         point_code, display_name, unit, value_type, enabled);

struct history_point_series {
    std::string point_id;
    std::string value_type;
    std::string series_table;
};

YLT_REFL(history_point_series, point_id, value_type, series_table);

struct history_event_identity {
    std::string event_id;
    std::string point_id;
    std::int64_t source_time_us{0};
    std::string payload_sha256;
};

YLT_REFL(history_event_identity, event_id, point_id, source_time_us, payload_sha256);

struct history_quarantine {
    std::string stream_scope;
    std::string stream_id;
    std::string event_id;
    std::string reason;
    std::string raw_payload;
};

YLT_REFL(history_quarantine, stream_scope, stream_id, event_id, reason, raw_payload);

struct QuarantineKey {
    std::string stream_id;
};

constexpr std::string_view get_alias_struct_name(QuarantineKey*) { return "history_quarantine"; }

YLT_REFL(QuarantineKey, stream_id);

}  // namespace ems::database::mysql_rows

namespace ems::database {
namespace {

core::Error mysql_error_result(unsigned int code, core::WriteEffect effect, const char* action) {
    auto category = core::ErrorCode::Unavailable;
    if (code == 1062 || code == 1205 || code == 1213) category = core::ErrorCode::Conflict;
    if (code == 1044 || code == 1045 || code == 1142) category = core::ErrorCode::PermissionDenied;
    if (code == 1054 || code == 1146) category = core::ErrorCode::SchemaMismatch;
    if (code == 3024) category = core::ErrorCode::DeadlineExceeded;
    return core::make_error(category, effect, std::string("MySQL ") + action + " failed");
}

bool mysql_thread_ready() {
    // Initialize the process-wide client before workers call its per-thread initializer.
    // Keep it alive until process exit; another shared Repository may still own a connection.
    static const bool library_ready = mysql_library_init(0, nullptr, nullptr) == 0;
    if (!library_ready) return false;

    struct ThreadContext {
        bool ready{mysql_thread_init() == 0};

        ~ThreadContext() {
            if (ready) mysql_thread_end();
        }
    };

    thread_local ThreadContext context;
    return context.ready;
}

// The library invokes connect/ping synchronously on the borrowing thread.
thread_local const core::CallContext* pool_call_context = nullptr;

struct PoolCallContext {
    explicit PoolCallContext(const core::CallContext& ctx) : previous(pool_call_context) {
        pool_call_context = &ctx;
    }

    ~PoolCallContext() { pool_call_context = previous; }

    const core::CallContext* previous;
};

class MySqlConnection final : public detail::Connection {
   public:
    bool connect(const std::tuple<std::string, std::string, std::string, std::string,
                                  std::optional<int>, std::optional<int>>& args) {
        MySqlConnectionOptions options;
        options.host = std::get<0>(args);
        options.user = std::get<1>(args);
        options.password = std::get<2>(args);
        options.database = std::get<3>(args);
        options.io_timeout_seconds = static_cast<unsigned int>(std::get<4>(args).value_or(5));
        options.port = static_cast<std::uint16_t>(std::get<5>(args).value_or(3306));
        timeout_ = options.io_timeout_seconds;
        return pool_call_context && core::is_ok(connect(options, *pool_call_context));
    }

    bool ping() {
        return pool_call_context && core::is_ok(prepare(*pool_call_context)) && orm.ping();
    }

    bool has_error() const { return orm.has_error(); }

    auto get_latest_operate_time() const { return last_operation_; }

    void update_operate_time() { last_operation_ = std::chrono::system_clock::now(); }

    ~MySqlConnection() override {
        mysql_thread_ready();
        orm.disconnect();
    }

    core::Status connect(const MySqlConnectionOptions& options, const core::CallContext& ctx) {
        if (!mysql_thread_ready())
            return mysql_error_result(0, core::WriteEffect::NotApplied, "thread init");
        if (core::has_expired(ctx))
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "MySQL connect deadline");
        const int seconds = budget_seconds(ctx);
        if (!orm.connect(options.host, options.user, options.password, options.database,
                         std::optional<int>(seconds), std::optional<int>(options.port))) {
            return mysql_error_result(orm.get_last_error_code(), core::WriteEffect::NotApplied,
                                      "connect");
        }
        // Reconnection inside a transaction could silently turn later statements into autocommit.
        const bool reconnect = false;
        if (mysql_options(orm.native_handle(), MYSQL_OPT_RECONNECT, &reconnect) != 0) {
            return mysql_error_result(mysql_errno(orm.native_handle()),
                                      core::WriteEffect::NotApplied, "session options");
        }
        auto prepared = prepare(ctx);
        if (!core::is_ok(prepared)) return prepared;
        if (mysql_set_character_set(orm.native_handle(), "utf8mb4") != 0) {
            return mysql_error_result(mysql_errno(orm.native_handle()),
                                      core::WriteEffect::NotApplied, "charset");
        }
        orm.set_enable_transaction(false);
        prepared = prepare(ctx);
        if (!core::is_ok(prepared)) return prepared;
        if (!orm.execute("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED")) {
            return mysql_error_result(orm.get_last_error_code(), core::WriteEffect::NotApplied,
                                      "isolation");
        }
        return core::ok_status();
    }

    core::Status prepare(const core::CallContext& ctx) {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "MySQL operation deadline");
        }
        if (!mysql_thread_ready())
            return mysql_error_result(0, core::WriteEffect::NotApplied, "thread init");
        const unsigned int seconds = static_cast<unsigned int>(budget_seconds(ctx));
        if (mysql_options(orm.native_handle(), MYSQL_OPT_READ_TIMEOUT, &seconds) != 0 ||
            mysql_options(orm.native_handle(), MYSQL_OPT_WRITE_TIMEOUT, &seconds) != 0) {
            return mysql_error_result(mysql_errno(orm.native_handle()),
                                      core::WriteEffect::NotApplied, "timeouts");
        }
        return core::ok_status();
    }

    core::Status begin(const core::CallContext& ctx) override {
        auto prepared = prepare(ctx);
        if (!core::is_ok(prepared)) return prepared;
        if (!orm.begin())
            return mysql_error_result(orm.get_last_error_code(), core::WriteEffect::NotApplied,
                                      "begin");
        return core::ok_status();
    }

    core::Status commit(const core::CallContext& ctx) override {
        auto prepared = prepare(ctx);
        if (!core::is_ok(prepared)) return prepared;
        if (!orm.commit())
            return mysql_error_result(orm.get_last_error_code(), core::WriteEffect::Unknown,
                                      "commit");
        return core::ok_status();
    }

    core::Status rollback(const core::CallContext& ctx) override {
        auto prepared = prepare(ctx);
        if (!core::is_ok(prepared)) return prepared;
        if (!orm.rollback())
            return mysql_error_result(orm.get_last_error_code(), core::WriteEffect::Unknown,
                                      "rollback");
        return core::ok_status();
    }

    ormpp::mysql orm;

   private:
    int budget_seconds(const core::CallContext& ctx) const {
        const auto milliseconds = std::max<std::int64_t>(1, core::remaining_budget_ms(ctx));
        return static_cast<int>(std::min<std::int64_t>(timeout_, (milliseconds + 999) / 1000));
    }

    unsigned int timeout_{5};
    std::chrono::system_clock::time_point last_operation_{std::chrono::system_clock::now()};
};

using MySqlPool = ormpp::connection_pool<MySqlConnection>;

struct ProcessMySqlContext {
    std::timed_mutex mutex;
    std::optional<MySqlConnectionOptions> options;
    std::shared_ptr<detail::TransactionDatabase> database;
};

bool same_pool_options(const MySqlConnectionOptions& a, const MySqlConnectionOptions& b) {
    return a.host == b.host && a.port == b.port && a.user == b.user && a.password == b.password &&
           a.database == b.database && a.max_connections == b.max_connections &&
           a.acquire_timeout == b.acquire_timeout && a.io_timeout_seconds == b.io_timeout_seconds;
}

core::Result<std::shared_ptr<detail::TransactionDatabase>> process_database(
    const MySqlConnectionOptions& options, const core::CallContext& ctx) {
    static ProcessMySqlContext process;
    std::unique_lock<std::timed_mutex> lock(process.mutex, std::defer_lock);
    if (!lock.try_lock_until(ctx.deadline))
        return core::make_error(core::ErrorCode::DeadlineExceeded, core::WriteEffect::NotApplied,
                                "MySQL initialization deadline");
    if (process.options && !same_pool_options(*process.options, options))
        return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                "MySQL pool already initialized with different configuration");
    if (process.database) return process.database;
    if (core::has_expired(ctx))
        return core::make_error(core::ErrorCode::DeadlineExceeded, core::WriteEffect::NotApplied,
                                "MySQL initialization deadline");
    PoolCallContext call(ctx);
    try {
        MySqlPool::instance().init(static_cast<int>(options.max_connections), options.host,
                                   options.user, options.password, options.database,
                                   static_cast<int>(options.io_timeout_seconds), options.port);
    } catch (...) {
        return mysql_error_result(ormpp::mysql{}.get_last_error_code(),
                                  core::WriteEffect::NotApplied, "pool initialization");
    }
    auto database = std::make_shared<detail::TransactionDatabase>(
        detail::TransactionOptions{},
        [options](const core::CallContext& context) -> core::Result<detail::ConnectionLease> {
            if (!mysql_thread_ready())
                return mysql_error_result(0, core::WriteEffect::NotApplied, "thread init");
            PoolCallContext call(context);
            ormpp::mysql::reset_error();
            const auto deadline = std::min(
                context.deadline, std::chrono::steady_clock::now() + options.acquire_timeout);
            auto connection = MySqlPool::instance().get_until(deadline);
            if (!connection) {
                if (core::has_expired(context))
                    return core::make_error(core::ErrorCode::DeadlineExceeded,
                                            core::WriteEffect::NotApplied, "MySQL borrow deadline");
                if (ormpp::mysql{}.has_error())
                    return mysql_error_result(ormpp::mysql{}.get_last_error_code(),
                                              core::WriteEffect::NotApplied, "pool borrow");
                return core::make_error(core::ErrorCode::ResourceExhausted,
                                        core::WriteEffect::NotApplied, "MySQL pool exhausted");
            }
            return detail::ConnectionLease{
                std::move(connection),
                [](std::shared_ptr<detail::Connection> borrowed, bool healthy) {
                    MySqlPool::instance().return_back(
                        std::static_pointer_cast<MySqlConnection>(std::move(borrowed)), healthy);
                }};
        });
    process.options = options;
    process.database = database;
    return database;
}

bool bounded_id(const std::string& id, std::size_t length = 64) {
    return !id.empty() && id.size() <= length;
}

core::Result<std::uint64_t> version_number(const std::string& version) {
    std::uint64_t number = 0;
    if (version.empty() || version.size() > 20 || (version.size() > 1 && version.front() == '0')) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "invalid history version");
    }
    for (const char digit : version) {
        if (digit < '0' || digit > '9' ||
            number > (std::numeric_limits<std::uint64_t>::max() - (digit - '0')) / 10) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "invalid history version");
        }
        number = number * 10 + (digit - '0');
    }
    return number;
}

core::Result<ValueType> parse_type(const std::string& value) {
    if (value == "numeric") return ValueType::Numeric;
    if (value == "integer") return ValueType::Integer;
    if (value == "boolean") return ValueType::Boolean;
    if (value == "text") return ValueType::Text;
    return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                            "invalid series value type");
}

class HistoryRepositories final : public IHistoryCatalogRepository, public IHistoryEventRepository {
   public:
    explicit HistoryRepositories(std::shared_ptr<detail::TransactionDatabase> pool)
        : pool_(std::move(pool)) {}

    core::Result<MySqlConnection*> session(ITransaction& tx, const core::CallContext& ctx) {
        auto borrowed = pool_->connection(tx, ctx);
        if (!core::is_ok(borrowed)) return core::error_of(borrowed);
        auto* connection = dynamic_cast<MySqlConnection*>(core::value_of(borrowed));
        if (!connection)
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "not a MySQL transaction");
        auto prepared = connection->prepare(ctx);
        if (!core::is_ok(prepared)) return core::error_of(prepared);
        return connection;
    }

    core::Error failed(ITransaction& tx, MySqlConnection& connection, const char* action,
                       core::WriteEffect effect = core::WriteEffect::NotApplicable) {
        const auto code = connection.orm.get_last_error_code();
        pool_->invalidate(tx);
        return mysql_error_result(code, effect, action);
    }

    core::Result<PointSnapshot> find_point(ITransaction& tx, const std::string& scope_kind,
                                           const std::string& scope_id, const std::string& version,
                                           const std::string& point_id,
                                           const core::CallContext& ctx) override {
        auto number = version_number(version);
        if (!core::is_ok(number)) return core::error_of(number);
        if ((scope_kind != "unit" && scope_kind != "station") || !bounded_id(scope_id) ||
            !bounded_id(point_id)) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "invalid history identity");
        }
        auto result = session(tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto& connection = *core::value_of(result);
        auto& orm = connection.orm;
        const auto headers = orm.query_s<mysql_rows::history_point_version>(
            "scope_kind=? AND scope_id=? AND config_version=?", scope_kind, scope_id,
            core::value_of(number));
        if (orm.has_error()) return failed(tx, connection, "version query");
        if (headers.empty() || !headers.front().ready_at || headers.front().ready_at->empty()) {
            return core::make_error(core::ErrorCode::NotReady, core::WriteEffect::NotApplicable,
                                    "point version not ready");
        }
        const auto rows = orm.query_s<mysql_rows::history_point_snapshot>(
            "scope_kind=? AND scope_id=? AND config_version=? AND point_id=?", scope_kind, scope_id,
            core::value_of(number), point_id);
        if (orm.has_error()) return failed(tx, connection, "snapshot query");
        if (rows.empty())
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable,
                                    "point absent from ready version");
        auto mapping = find_series(tx, point_id, ctx);
        if (!core::is_ok(mapping)) return core::error_of(mapping);
        const auto& row = rows.front();
        auto type = parse_type(row.value_type);
        if (!core::is_ok(type)) return core::error_of(type);
        if (core::value_of(type) != core::value_of(mapping).value_type) {
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                    "series type mismatch");
        }
        return PointSnapshot{row.scope_kind,
                             row.scope_id,
                             std::to_string(row.config_version),
                             row.point_id,
                             row.owner_device_id,
                             row.point_code,
                             row.display_name,
                             row.unit.value_or(""),
                             core::value_of(type),
                             core::value_of(mapping).series_key,
                             row.enabled};
    }

    core::Result<SeriesMapping> find_series(ITransaction& tx, const std::string& point_id,
                                            const core::CallContext& ctx) override {
        if (!bounded_id(point_id))
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "invalid point ID");
        auto result = session(tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto& connection = *core::value_of(result);
        const auto rows =
            connection.orm.query_s<mysql_rows::history_point_series>("point_id=?", point_id);
        if (connection.orm.has_error()) return failed(tx, connection, "series query");
        if (rows.empty())
            return core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable,
                                    "point series not found");
        auto type = parse_type(rows.front().value_type);
        if (!core::is_ok(type)) return core::error_of(type);
        return SeriesMapping{point_id, core::value_of(type), rows.front().series_table};
    }

    core::Status register_event(ITransaction& tx, const Sample& sample,
                                const core::CallContext& ctx) override {
        if (!bounded_id(sample.event_id) || !bounded_id(sample.point_id) ||
            sample.payload_sha256.size() != 64 || sample.source_time_us <= 0 ||
            !std::all_of(sample.payload_sha256.begin(), sample.payload_sha256.end(), [](char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "invalid event identity");
        }
        auto result = session(tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto& connection = *core::value_of(result);
        auto& orm = connection.orm;
        mysql_rows::history_event_identity row{sample.event_id, sample.point_id,
                                               sample.source_time_us, sample.payload_sha256};
        const auto inserted = orm.insert(row);
        if (inserted == 1 && !orm.has_error()) return core::ok_status();
        if (inserted != 1 && (!orm.has_error() || orm.get_last_error_code() != 1062)) {
            return failed(tx, connection, "identity insert", core::WriteEffect::NotApplied);
        }
        const auto existing =
            orm.query_s<mysql_rows::history_event_identity>("event_id=?", sample.event_id);
        if (orm.has_error() || existing.empty()) return failed(tx, connection, "identity query");
        const auto& saved = existing.front();
        if (saved.point_id != row.point_id || saved.source_time_us != row.source_time_us ||
            saved.payload_sha256 != row.payload_sha256) {
            return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "event identity conflict");
        }
        return core::ok_status();
    }

    core::Status quarantine(ITransaction& tx, const QuarantineRecord& record,
                            const core::CallContext& ctx) override {
        if (!bounded_id(record.scope, 128) || !bounded_id(record.stream_id) ||
            record.event_id.size() > 64 || !bounded_id(record.reason, 256) ||
            record.payload.size() > 1024 * 1024) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "invalid quarantine record");
        }
        auto result = session(tx, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto& connection = *core::value_of(result);
        auto& orm = connection.orm;
        mysql_rows::history_quarantine row{record.scope, record.stream_id, record.event_id,
                                           record.reason, record.payload};
        const auto inserted = orm.insert(row);
        if (inserted == 1 && !orm.has_error()) return core::ok_status();
        if (inserted != 1 && (!orm.has_error() || orm.get_last_error_code() != 1062)) {
            return failed(tx, connection, "quarantine insert", core::WriteEffect::NotApplied);
        }
        // Compare the BLOB in the server. Fetching it through ormpp's fixed-size text buffer
        // would truncate embedded NULs or large quarantined frames.
        const auto existing = orm.query_s<mysql_rows::QuarantineKey>(
            "stream_scope=? AND stream_id=? AND BINARY event_id=BINARY ? "
            "AND BINARY reason=BINARY ? AND raw_payload=?",
            record.scope, record.stream_id, record.event_id, record.reason, record.payload);
        if (orm.has_error()) return failed(tx, connection, "quarantine query");
        if (existing.empty()) {
            return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "quarantine identity conflict");
        }
        return core::ok_status();
    }

   private:
    std::shared_ptr<detail::TransactionDatabase> pool_;
};

}  // namespace

core::Result<MySqlDatabaseContext> connect_mysql_database(const MySqlConnectionOptions& options,
                                                          const core::CallContext& ctx) {
    if (options.host.empty() || options.port == 0 || options.user.empty() ||
        options.database.empty() || options.max_connections == 0 || options.max_connections > 128 ||
        options.acquire_timeout.count() <= 0 || options.io_timeout_seconds == 0 ||
        options.io_timeout_seconds > 60) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "invalid MySQL context options");
    }
    auto shared = process_database(options, ctx);
    if (!core::is_ok(shared)) return core::error_of(shared);
    auto pool = core::value_of(shared);
    auto probe = pool->begin(ctx);
    if (!core::is_ok(probe)) return core::error_of(probe);
    auto& tx = *std::get<0>(probe);
    auto borrowed = pool->connection(tx, ctx);
    if (!core::is_ok(borrowed)) return core::error_of(borrowed);
    auto& connection = *static_cast<MySqlConnection*>(core::value_of(borrowed));
    if (options.require_history_schema) {
        // Select reflected columns without rows. Missing migrations fail before the process is ready.
        connection.orm.query_s<mysql_rows::history_point_version>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "history schema");
        connection.orm.query_s<mysql_rows::history_point_snapshot>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "history schema");
        connection.orm.query_s<mysql_rows::history_point_series>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "history schema");
        connection.orm.query_s<mysql_rows::history_event_identity>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "history schema");
        connection.orm.query_s<mysql_rows::history_quarantine>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "history schema");
    }
    if (options.require_configuration_schema) {
        connection.orm.query_s<DeviceRecord>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
        connection.orm.query_s<ChannelRecord>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
        connection.orm.query_s<AnalogPoint>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
        connection.orm.query_s<StatusPoint>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
        connection.orm.query_s<ControlPoint>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
        connection.orm.query_s<SetpointPoint>("1=0");
        if (connection.orm.has_error())
            return mysql_error_result(connection.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration schema");
    }
    auto released = tx.rollback(ctx);
    if (!core::is_ok(released)) return core::error_of(released);
    auto repositories = std::make_shared<HistoryRepositories>(pool);
    auto configuration = std::make_shared<detail::OrmConfigurationRepository<MySqlConnection>>(
        [repositories](ITransaction& transaction, const core::CallContext& context) {
            return repositories->session(transaction, context);
        },
        [pool](ITransaction& transaction) { pool->invalidate(transaction); },
        [](MySqlConnection& driver) {
            return mysql_error_result(driver.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "configuration operation");
        },
        true);
    auto device_config = std::make_shared<detail::OrmDeviceConfigRepository<MySqlConnection>>(
        [repositories](ITransaction& transaction, const core::CallContext& context) {
            return repositories->session(transaction, context);
        },
        [pool](ITransaction& transaction) { pool->invalidate(transaction); },
        [](MySqlConnection& driver) {
            return mysql_error_result(driver.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied,
                                      "device configuration operation");
        });
    auto management = std::make_shared<detail::OrmDeviceManagementStore<MySqlConnection>>(
        [repositories](ITransaction& transaction, const core::CallContext& context) {
            return repositories->session(transaction, context);
        },
        [pool](ITransaction& transaction) { pool->invalidate(transaction); },
        [](MySqlConnection& driver) {
            return mysql_error_result(driver.orm.get_last_error_code(),
                                      core::WriteEffect::NotApplied, "device management operation");
        });
    return MySqlDatabaseContext{pool,          repositories,  repositories,
                                configuration, device_config, management};
}

}  // namespace ems::database
