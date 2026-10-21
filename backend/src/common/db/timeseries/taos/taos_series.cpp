#include <common/database/timeseries/factory.h>
#include <taos.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace ems::database {
namespace {

core::Error taos_error(const char* operation, core::WriteEffect effect) {
    return core::make_error(core::ErrorCode::Unavailable, effect,
                            std::string("TDengine ") + operation + " failed");
}

struct ConnectionCloser {
    void operator()(TAOS* connection) const {
        if (connection) taos_close(connection);
    }
};

struct StatementCloser {
    void operator()(TAOS_STMT2* statement) const {
        if (statement) taos_stmt2_close(statement);
    }
};

struct ResultCloser {
    void operator()(TAOS_RES* result) const {
        if (result) taos_free_result(result);
    }
};

using TaosPtr = std::unique_ptr<TAOS, ConnectionCloser>;
using StatementPtr = std::unique_ptr<TAOS_STMT2, StatementCloser>;
using ResultPtr = std::unique_ptr<TAOS_RES, ResultCloser>;

bool valid_table(const std::string& table) {
    return table.size() >= 3 && table.size() <= 96 && table.compare(0, 2, "p_") == 0 &&
           std::all_of(table.begin() + 2, table.end(), [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

template <typename T>
bool read_scalar(TAOS_ROW row, const int* lengths, int index, T& value) {
    if (!row[index] || lengths[index] != static_cast<int>(sizeof(T))) return false;
    std::memcpy(&value, row[index], sizeof(T));
    return true;
}

std::string read_text(TAOS_ROW row, const int* lengths, int index) {
    if (!row[index] || lengths[index] < 0) return {};
    return std::string(static_cast<const char*>(row[index]), lengths[index]);
}

core::Result<Sample> read_sample(TAOS_ROW row, const int* lengths, const SeriesMapping& mapping) {
    Sample sample;
    sample.point_id = read_text(row, lengths, 15);
    sample.event_id = read_text(row, lengths, 1);
    sample.time_reason = read_text(row, lengths, 6);
    sample.quality_reason = read_text(row, lengths, 7);
    sample.payload_sha256 = read_text(row, lengths, 8);
    sample.scope_kind = read_text(row, lengths, 9);
    sample.scope_id = read_text(row, lengths, 10);
    sample.config_version = read_text(row, lengths, 11);
    sample.source_id = read_text(row, lengths, 12);
    sample.source_epoch = read_text(row, lengths, 13);
    sample.sequence = read_text(row, lengths, 14);
    sample.series_key = mapping.series_key;
    sample.value_type = mapping.value_type;
    std::int8_t origin = 0;
    if (sample.point_id != mapping.point_id || sample.event_id.empty() ||
        !read_scalar(row, lengths, 0, sample.source_time_us) ||
        !read_scalar(row, lengths, 3, sample.quality) ||
        !read_scalar(row, lengths, 4, sample.ingest_time_us) ||
        !read_scalar(row, lengths, 5, origin)) {
        return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                "invalid TDengine history row");
    }
    sample.time_origin = static_cast<std::uint8_t>(origin);
    if (row[2]) {
        switch (mapping.value_type) {
            case ValueType::Numeric: {
                double value = 0;
                if (!read_scalar(row, lengths, 2, value) || !std::isfinite(value)) {
                    return core::make_error(core::ErrorCode::CorruptData,
                                            core::WriteEffect::NotApplicable, "numeric row");
                }
                sample.value = value;
                break;
            }
            case ValueType::Integer: {
                std::int64_t value = 0;
                if (!read_scalar(row, lengths, 2, value)) {
                    return core::make_error(core::ErrorCode::CorruptData,
                                            core::WriteEffect::NotApplicable, "integer row");
                }
                sample.value = value;
                break;
            }
            case ValueType::Boolean: {
                std::int8_t value = 0;
                if (!read_scalar(row, lengths, 2, value)) {
                    return core::make_error(core::ErrorCode::CorruptData,
                                            core::WriteEffect::NotApplicable, "boolean row");
                }
                sample.value = value != 0;
                break;
            }
            case ValueType::Text:
                sample.value = read_text(row, lengths, 2);
                break;
        }
    }
    return sample;
}

const char* insert_sql(ValueType type) {
    switch (type) {
        case ValueType::Numeric:
            return "INSERT INTO ? USING ems_history.telemetry_numeric TAGS(?) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
        case ValueType::Integer:
            return "INSERT INTO ? USING ems_history.telemetry_integer TAGS(?) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
        case ValueType::Boolean:
            return "INSERT INTO ? USING ems_history.telemetry_boolean TAGS(?) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
        case ValueType::Text:
            return "INSERT INTO ? USING ems_history.telemetry_text TAGS(?) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    }
    return nullptr;
}

TAOS_STMT2_BIND bind_value(int type, void* buffer, std::int32_t* length, char* is_null = nullptr) {
    TAOS_STMT2_BIND result{};
    result.buffer_type = type;
    result.buffer = buffer;
    result.length = length;
    result.is_null = is_null;
    result.num = 1;
    return result;
}

class TaosSeriesStore final : public ITimeSeriesDatabase {
   public:
    explicit TaosSeriesStore(TaosPtr connection) : connection_(std::move(connection)) {}

    core::Status write(const Sample& sample, const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "TDengine deadline");
        }
        if (!valid_table(sample.series_key)) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "invalid series table");
        }
        const auto* sql = insert_sql(sample.value_type);
        if (!sql) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "invalid value type");
        }
        const bool null_value = std::holds_alternative<std::monostate>(sample.value);
        const bool matching_value = null_value ||
                                    (sample.value_type == ValueType::Numeric &&
                                     std::holds_alternative<double>(sample.value) &&
                                     std::isfinite(std::get<double>(sample.value))) ||
                                    (sample.value_type == ValueType::Integer &&
                                     std::holds_alternative<std::int64_t>(sample.value)) ||
                                    (sample.value_type == ValueType::Boolean &&
                                     std::holds_alternative<bool>(sample.value)) ||
                                    (sample.value_type == ValueType::Text &&
                                     std::holds_alternative<std::string>(sample.value));
        if (!matching_value) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "sample value type mismatch");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        TAOS_STMT2_OPTION options{};
        StatementPtr statement(taos_stmt2_init(connection_.get(), &options));
        if (!statement || taos_stmt2_prepare(statement.get(), sql, 0) != 0) {
            return taos_error("prepare", core::WriteEffect::NotApplied);
        }
        std::string table = sample.series_key;
        char* table_names[] = {table.data()};
        std::int64_t timestamp = sample.source_time_us;
        std::int64_t ingest_timestamp = sample.ingest_time_us;
        std::int32_t quality = sample.quality;
        std::int8_t time_origin = static_cast<std::int8_t>(sample.time_origin);
        double numeric_value = 0;
        std::int64_t integer_value = 0;
        std::int8_t boolean_value = 0;
        std::string text_value;
        int value_type = TSDB_DATA_TYPE_DOUBLE;
        void* value_buffer = &numeric_value;
        switch (sample.value_type) {
            case ValueType::Numeric:
                if (const auto* value = std::get_if<double>(&sample.value)) numeric_value = *value;
                value_type = TSDB_DATA_TYPE_DOUBLE;
                break;
            case ValueType::Integer:
                if (const auto* value = std::get_if<std::int64_t>(&sample.value)) {
                    integer_value = *value;
                }
                value_buffer = &integer_value;
                value_type = TSDB_DATA_TYPE_BIGINT;
                break;
            case ValueType::Boolean:
                if (const auto* value = std::get_if<bool>(&sample.value)) {
                    boolean_value = static_cast<std::int8_t>(*value);
                }
                value_buffer = &boolean_value;
                value_type = TSDB_DATA_TYPE_BOOL;
                break;
            case ValueType::Text:
                if (const auto* value = std::get_if<std::string>(&sample.value))
                    text_value = *value;
                value_buffer = text_value.data();
                value_type = TSDB_DATA_TYPE_BINARY;
                break;
        }
        std::array<std::int32_t, 16> lengths{
            static_cast<std::int32_t>(sample.point_id.size()),
            static_cast<std::int32_t>(sizeof(timestamp)),
            static_cast<std::int32_t>(sample.event_id.size()),
            static_cast<std::int32_t>(
                sample.value_type == ValueType::Text      ? text_value.size()
                : sample.value_type == ValueType::Numeric ? sizeof(numeric_value)
                : sample.value_type == ValueType::Integer ? sizeof(integer_value)
                                                          : sizeof(boolean_value)),
            static_cast<std::int32_t>(sizeof(quality)),
            static_cast<std::int32_t>(sizeof(ingest_timestamp)),
            static_cast<std::int32_t>(sizeof(time_origin)),
            static_cast<std::int32_t>(sample.time_reason.size()),
            static_cast<std::int32_t>(sample.quality_reason.size()),
            static_cast<std::int32_t>(sample.payload_sha256.size()),
            static_cast<std::int32_t>(sample.scope_kind.size()),
            static_cast<std::int32_t>(sample.scope_id.size()),
            static_cast<std::int32_t>(sample.config_version.size()),
            static_cast<std::int32_t>(sample.source_id.size()),
            static_cast<std::int32_t>(sample.source_epoch.size()),
            static_cast<std::int32_t>(sample.sequence.size())};
        char value_is_null = null_value ? 1 : 0;
        TAOS_STMT2_BIND tags[] = {bind_value(
            TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.point_id.data()), &lengths[0])};
        TAOS_STMT2_BIND columns[] = {
            bind_value(TSDB_DATA_TYPE_TIMESTAMP, &timestamp, &lengths[1]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.event_id.data()),
                       &lengths[2]),
            bind_value(value_type, value_buffer, &lengths[3], &value_is_null),
            bind_value(TSDB_DATA_TYPE_INT, &quality, &lengths[4]),
            bind_value(TSDB_DATA_TYPE_TIMESTAMP, &ingest_timestamp, &lengths[5]),
            bind_value(TSDB_DATA_TYPE_TINYINT, &time_origin, &lengths[6]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.time_reason.data()),
                       &lengths[7]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.quality_reason.data()),
                       &lengths[8]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.payload_sha256.data()),
                       &lengths[9]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.scope_kind.data()),
                       &lengths[10]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.scope_id.data()),
                       &lengths[11]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.config_version.data()),
                       &lengths[12]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.source_id.data()),
                       &lengths[13]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.source_epoch.data()),
                       &lengths[14]),
            bind_value(TSDB_DATA_TYPE_BINARY, const_cast<char*>(sample.sequence.data()),
                       &lengths[15])};
        TAOS_STMT2_BIND* tag_bindings[] = {tags};
        TAOS_STMT2_BIND* column_bindings[] = {columns};
        TAOS_STMT2_BINDV bindings{1, table_names, tag_bindings, column_bindings};
        if (taos_stmt2_bind_param(statement.get(), &bindings, -1) != 0) {
            return taos_error("bind", core::WriteEffect::NotApplied);
        }
        int affected = 0;
        if (taos_stmt2_exec(statement.get(), &affected) != 0) {
            return taos_error("insert", core::WriteEffect::Unknown);
        }
        return core::ok_status();
    }

    core::Result<std::vector<Sample>> scan(const HistoryQuery& query,
                                           const std::vector<SeriesMapping>& mappings,
                                           std::uint32_t limit,
                                           const core::CallContext& ctx) override {
        if (core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplicable, "TDengine deadline");
        }
        if (limit == 0 || limit > 1001 || mappings.size() > 64 ||
            (query.scope_kind != "station" && query.scope_kind != "unit")) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "history scan bounds");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Sample> samples;
        for (const auto& mapping : mappings) {
            if (core::has_expired(ctx)) {
                return core::make_error(core::ErrorCode::DeadlineExceeded,
                                        core::WriteEffect::NotApplicable, "TDengine deadline");
            }
            if (!valid_table(mapping.series_key)) {
                return core::make_error(core::ErrorCode::CorruptData,
                                        core::WriteEffect::NotApplicable, "series mapping");
            }
            std::string sql =
                "SELECT ts,event_id,sample_value,quality,ingest_ts,time_origin,time_reason,"
                "quality_reason,payload_sha256,scope_kind,scope_id,config_version,source_id,"
                "source_epoch,sequence,point_id FROM ems_history." +
                mapping.series_key + " WHERE ts >= " + std::to_string(query.start_us) +
                " AND ts < " + std::to_string(query.end_us) + " AND scope_kind = '" +
                query.scope_kind + "' AND scope_id = ?";
            bool bind_event_id = false;
            if (query.cursor) {
                const auto& cursor = *query.cursor;
                if (mapping.point_id == cursor.point_id) {
                    sql += " AND (ts > " + std::to_string(cursor.source_time_us) +
                           " OR (ts = " + std::to_string(cursor.source_time_us) +
                           " AND event_id > ?))";
                    bind_event_id = true;
                } else {
                    sql += " AND ts " +
                           std::string(mapping.point_id > cursor.point_id ? ">= " : "> ") +
                           std::to_string(cursor.source_time_us);
                }
            }
            sql += " ORDER BY ts,event_id LIMIT " + std::to_string(limit);
            TAOS_STMT2_OPTION options{};
            StatementPtr statement(taos_stmt2_init(connection_.get(), &options));
            if (!statement || taos_stmt2_prepare(statement.get(), sql.c_str(), 0) != 0) {
                return taos_error("scan prepare", core::WriteEffect::NotApplicable);
            }
            std::int32_t scope_length = static_cast<std::int32_t>(query.scope_id.size());
            std::int32_t event_length =
                query.cursor ? static_cast<std::int32_t>(query.cursor->event_id.size()) : 0;
            std::vector<TAOS_STMT2_BIND> columns;
            columns.push_back(bind_value(TSDB_DATA_TYPE_BINARY,
                                         const_cast<char*>(query.scope_id.data()), &scope_length));
            if (bind_event_id) {
                columns.push_back(bind_value(TSDB_DATA_TYPE_BINARY,
                                             const_cast<char*>(query.cursor->event_id.data()),
                                             &event_length));
            }
            TAOS_STMT2_BIND* column_bindings[] = {columns.data()};
            TAOS_STMT2_BINDV bindings{1, nullptr, nullptr, column_bindings};
            int affected = 0;
            if (taos_stmt2_bind_param(statement.get(), &bindings, -1) != 0 ||
                taos_stmt2_exec(statement.get(), &affected) != 0) {
                return taos_error("scan execute", core::WriteEffect::NotApplicable);
            }
            ResultPtr result(taos_stmt2_result(statement.get()));
            if (!result || taos_errno(result.get()) != 0 ||
                taos_result_precision(result.get()) != 1 || taos_num_fields(result.get()) != 16) {
                return taos_error("scan result", core::WriteEffect::NotApplicable);
            }
            while (auto row = taos_fetch_row(result.get())) {
                const int* lengths = taos_fetch_lengths(result.get());
                if (!lengths) {
                    return taos_error("scan lengths", core::WriteEffect::NotApplicable);
                }
                auto sample = read_sample(row, lengths, mapping);
                if (!core::is_ok(sample)) return core::error_of(sample);
                samples.push_back(core::value_of(sample));
            }
            if (taos_errno(result.get()) != 0) {
                return taos_error("scan fetch", core::WriteEffect::NotApplicable);
            }
        }
        std::sort(samples.begin(), samples.end(), [](const Sample& left, const Sample& right) {
            return std::tie(left.source_time_us, left.point_id, left.event_id) <
                   std::tie(right.source_time_us, right.point_id, right.event_id);
        });
        if (samples.size() > limit) samples.resize(limit);
        return samples;
    }

   private:
    TaosPtr connection_;
    std::mutex mutex_;
};

}  // namespace

core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_taos_series(
    const TaosConnectionOptions& options) {
    const char* client_info = taos_get_client_info();
    const std::string client_version = client_info ? client_info : "";
    if (client_version.rfind("3.4.2", 0) != 0 || options.database != "ems_history") {
        return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                "TDengine 3.4.2 client and ems_history database required");
    }
    TaosPtr connection(taos_connect(options.host.c_str(), options.user.c_str(),
                                    options.password.c_str(), options.database.c_str(),
                                    options.port));
    if (!connection) return taos_error("connect", core::WriteEffect::NotApplicable);
    const char* server_info = taos_get_server_info(connection.get());
    const std::string server_version = server_info ? server_info : "";
    if (server_version.rfind("3.4.2", 0) != 0) {
        return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplicable,
                                "TDengine 3.4.2 server required");
    }
    return std::unique_ptr<ITimeSeriesDatabase>(new TaosSeriesStore(std::move(connection)));
}

}  // namespace ems::database
