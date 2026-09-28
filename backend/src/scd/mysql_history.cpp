#include <storage/native_adapters.h>

#include <cstdint>
#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <ormpp/dbng.hpp>
#include <ormpp/mysql.hpp>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ems::storage {

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

namespace {

using SnapshotTuple =
    std::tuple<std::string, std::string, std::string, std::string, std::string, std::string,
               std::string, std::string, std::string, std::string, std::string, std::string>;

core::Error database_error(const char* action, core::WriteEffect effect) {
    return core::make_error(core::ErrorCode::Unavailable, effect,
                            std::string("ormpp MySQL ") + action + " failed");
}

core::Result<ValueType> parse_type(const std::string& value) {
    if (value == "numeric") return ValueType::Numeric;
    if (value == "integer") return ValueType::Integer;
    if (value == "boolean") return ValueType::Boolean;
    if (value == "text") return ValueType::Text;
    return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                            "invalid series value type");
}

class OrmppHistoryRepository final : public IHistoryRepository {
   public:
    bool connect(const MySqlConnectionOptions& options) {
        return database_.connect(options.host, options.user, options.password, options.database,
                                 std::optional<int>(5), std::optional<int>(options.port));
    }

    core::Result<PointSnapshot> find_point(const std::string& scope_kind,
                                           const std::string& scope_id, const std::string& version,
                                           const std::string& point_id,
                                           const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplicable, "MySQL deadline");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr const char* kSelect =
            "SELECT s.scope_kind,s.scope_id,CAST(s.config_version AS CHAR),s.point_id,"
            "s.asset_id,s.point_code,s.display_name,COALESCE(s.unit,''),s.value_type,"
            "m.series_table,m.value_type,CAST(s.enabled AS CHAR) "
            "FROM history_point_snapshot s "
            "JOIN history_point_version v ON v.scope_kind=s.scope_kind AND "
            "v.scope_id=s.scope_id AND v.config_version=s.config_version "
            "JOIN history_point_series m ON m.point_id=s.point_id "
            "WHERE s.scope_kind=? AND s.scope_id=? AND s.config_version=? AND s.point_id=? "
            "AND v.ready_at IS NOT NULL";
        const auto rows =
            database_.query_s<SnapshotTuple>(kSelect, scope_kind, scope_id, version, point_id);
        if (database_.has_error())
            return database_error("snapshot query", core::WriteEffect::NotApplicable);
        if (rows.empty()) {
            constexpr const char* kHeader =
                "SELECT COALESCE(CAST(ready_at AS CHAR),'') FROM history_point_version "
                "WHERE scope_kind=? AND scope_id=? AND config_version=?";
            const auto headers =
                database_.query_s<std::tuple<std::string>>(kHeader, scope_kind, scope_id, version);
            if (database_.has_error())
                return database_error("version query", core::WriteEffect::NotApplicable);
            if (!headers.empty() && !std::get<0>(headers.front()).empty()) {
                return core::make_error(core::ErrorCode::InvalidArgument,
                                        core::WriteEffect::NotApplicable,
                                        "point absent from ready version");
            }
            return core::make_error(core::ErrorCode::NotReady, core::WriteEffect::NotApplicable,
                                    "point version not ready");
        }
        const auto& row = rows.front();
        auto type = parse_type(std::get<8>(row));
        if (!core::is_ok(type)) return core::error_of(type);
        if (std::get<8>(row) != std::get<10>(row)) {
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                    "series type mismatch");
        }
        return PointSnapshot{std::get<0>(row), std::get<1>(row),        std::get<2>(row),
                             std::get<3>(row), std::get<4>(row),        std::get<5>(row),
                             std::get<6>(row), std::get<7>(row),        core::value_of(type),
                             std::get<9>(row), std::get<11>(row) == "1"};
    }

    core::Status register_event(const Sample& sample, const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "MySQL deadline");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        history_event_identity row{sample.event_id, sample.point_id, sample.source_time_us,
                                   sample.payload_sha256};
        const auto inserted = database_.insert(row);
        if (inserted != 1 && !database_.has_error()) {
            return database_error("identity insert", core::WriteEffect::Unknown);
        }
        constexpr const char* kSelect =
            "SELECT point_id,source_time_us,payload_sha256 FROM history_event_identity "
            "WHERE event_id=?";
        const auto rows = database_.query_s<std::tuple<std::string, std::int64_t, std::string>>(
            kSelect, sample.event_id);
        if (database_.has_error() || rows.empty()) {
            return database_error("identity query", core::WriteEffect::Unknown);
        }
        const auto& existing = rows.front();
        if (std::get<0>(existing) != sample.point_id ||
            std::get<1>(existing) != sample.source_time_us ||
            std::get<2>(existing) != sample.payload_sha256) {
            return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "event identity conflict");
        }
        return core::ok_status();
    }

    core::Result<SeriesMapping> find_series(const std::string& point_id,
                                            const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplicable, "MySQL deadline");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr const char* kSelect =
            "SELECT value_type,series_table FROM history_point_series WHERE point_id=?";
        const auto rows =
            database_.query_s<std::tuple<std::string, std::string>>(kSelect, point_id);
        if (database_.has_error()) {
            return database_error("series query", core::WriteEffect::NotApplicable);
        }
        if (rows.empty()) {
            return core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable,
                                    "point series not found");
        }
        auto type = parse_type(std::get<0>(rows.front()));
        if (!core::is_ok(type)) return core::error_of(type);
        return SeriesMapping{point_id, core::value_of(type), std::get<1>(rows.front())};
    }

    core::Status quarantine(const QuarantineRecord& record, const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "MySQL deadline");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        history_quarantine row{record.scope, record.stream_id, record.event_id, record.reason,
                               record.payload};
        const auto inserted = database_.insert(row);
        if (inserted != 1 && !database_.has_error()) {
            return database_error("quarantine insert", core::WriteEffect::Unknown);
        }
        constexpr const char* kSelect =
            "SELECT COALESCE(event_id,''),reason,raw_payload FROM history_quarantine "
            "WHERE stream_scope=? AND stream_id=?";
        const auto rows = database_.query_s<std::tuple<std::string, std::string, std::string>>(
            kSelect, record.scope, record.stream_id);
        if (database_.has_error() || rows.empty()) {
            return database_error("quarantine query", core::WriteEffect::Unknown);
        }
        const auto& existing = rows.front();
        if (std::get<0>(existing) != record.event_id || std::get<1>(existing) != record.reason ||
            std::get<2>(existing) != record.payload) {
            return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                    "quarantine identity conflict");
        }
        return core::ok_status();
    }

   private:
    ormpp::dbng<ormpp::mysql> database_;
    std::mutex mutex_;
};

}  // namespace

core::Result<std::unique_ptr<IHistoryRepository>> connect_mysql_history(
    const MySqlConnectionOptions& options) {
    if (options.user.empty() || options.database.empty()) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "MySQL connection settings");
    }
    auto repository = std::make_unique<OrmppHistoryRepository>();
    if (!repository->connect(options)) {
        return database_error("connect", core::WriteEffect::NotApplicable);
    }
    return std::unique_ptr<IHistoryRepository>(std::move(repository));
}

}  // namespace ems::storage
