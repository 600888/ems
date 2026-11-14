#include <InfluxDB/InfluxDBBuilder.h>
#include <common/database/timeseries/factory.h>
#include <common/database/timeseries/series_key.h>
#include <date/date.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "options.h"

namespace ems::database {
namespace {
using Json = nlohmann::json;
constexpr std::int64_t kMaxTimeUs = 9223372036854775LL;
constexpr std::uint32_t kMaxRows = 1001;

const char* measurement(ValueType type) {
    switch (type) {
        case ValueType::Numeric:
            return "telemetry_numeric";
        case ValueType::Integer:
            return "telemetry_integer";
        case ValueType::Boolean:
            return "telemetry_boolean";
        case ValueType::Text:
            return "telemetry_text";
    }
    return nullptr;
}

std::string hex(const std::string& text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char ch : text) {
        result += digits[ch >> 4];
        result += digits[ch & 15];
    }
    return result;
}

bool valid_sample(const Sample& sample) {
    if (!valid_series_key(sample.series_key) || !measurement(sample.value_type) ||
        sample.point_id.empty() || sample.point_id.size() > 64 || sample.event_id.empty() ||
        sample.event_id.size() > 64 || sample.scope_id.empty() || sample.scope_id.size() > 64 ||
        (sample.scope_kind != "station" && sample.scope_kind != "unit") ||
        sample.source_time_us < -kMaxTimeUs || sample.source_time_us > kMaxTimeUs ||
        sample.config_revision.size() > 20 || sample.source_id.size() > 64 ||
        sample.source_epoch.size() > 20 || sample.sequence.size() > 20 ||
        sample.time_reason.size() > 128 || sample.quality_reason.size() > 256 ||
        sample.payload_sha256.size() > 64)
        return false;
    if (std::holds_alternative<std::monostate>(sample.value)) return true;
    switch (sample.value_type) {
        case ValueType::Numeric:
            return std::holds_alternative<double>(sample.value) &&
                   std::isfinite(std::get<double>(sample.value));
        case ValueType::Integer:
            return std::holds_alternative<std::int64_t>(sample.value);
        case ValueType::Boolean:
            return std::holds_alternative<bool>(sample.value);
        case ValueType::Text:
            return std::holds_alternative<std::string>(sample.value) &&
                   std::get<std::string>(sample.value).size() <= 1024;
    }
    return false;
}

Json record(const Sample& sample) {
    Json value = nullptr;
    std::visit(
        [&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (!std::is_same_v<T, std::monostate>) value = item;
        },
        sample.value);
    return {{"event_id", sample.event_id},
            {"point_id", sample.point_id},
            {"scope_kind", sample.scope_kind},
            {"scope_id", sample.scope_id},
            {"config_revision", sample.config_revision},
            {"series_key", sample.series_key},
            {"source_id", sample.source_id},
            {"source_epoch", sample.source_epoch},
            {"sequence", sample.sequence},
            {"quality_reason", sample.quality_reason},
            {"time_reason", sample.time_reason},
            {"payload_sha256", sample.payload_sha256},
            {"source_time_us", sample.source_time_us},
            {"ingest_time_us", sample.ingest_time_us},
            {"measurement_time_us", sample.measurement_time_us},
            {"quality", sample.quality},
            {"time_origin", sample.time_origin},
            {"value_type", static_cast<int>(sample.value_type)},
            {"value", value}};
}

std::int64_t integer(const Json& value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned() &&
         value.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX)))
        throw std::invalid_argument("integer");
    return value.get<std::int64_t>();
}

Sample read_record(const std::string& text) {
    const auto json = Json::parse(text);
    Sample sample;
    sample.event_id = json.at("event_id").get<std::string>();
    sample.point_id = json.at("point_id").get<std::string>();
    sample.scope_kind = json.at("scope_kind").get<std::string>();
    sample.scope_id = json.at("scope_id").get<std::string>();
    sample.config_revision = json.at("config_revision").get<std::string>();
    sample.series_key = json.at("series_key").get<std::string>();
    sample.source_id = json.at("source_id").get<std::string>();
    sample.source_epoch = json.at("source_epoch").get<std::string>();
    sample.sequence = json.at("sequence").get<std::string>();
    sample.quality_reason = json.at("quality_reason").get<std::string>();
    sample.time_reason = json.at("time_reason").get<std::string>();
    sample.payload_sha256 = json.at("payload_sha256").get<std::string>();
    sample.source_time_us = integer(json.at("source_time_us"));
    sample.ingest_time_us = integer(json.at("ingest_time_us"));
    sample.measurement_time_us = integer(json.at("measurement_time_us"));
    const auto quality = integer(json.at("quality"));
    const auto origin = integer(json.at("time_origin"));
    const auto type = integer(json.at("value_type"));
    if (quality < INT32_MIN || quality > INT32_MAX || origin < 0 || origin > UINT8_MAX ||
        type < 0 || type > 3)
        throw std::invalid_argument("record bounds");
    sample.quality = static_cast<std::int32_t>(quality);
    sample.time_origin = static_cast<std::uint8_t>(origin);
    sample.value_type = static_cast<ValueType>(type);
    const auto& value = json.at("value");
    if (!value.is_null()) {
        switch (sample.value_type) {
            case ValueType::Numeric:
                if (!value.is_number()) throw std::invalid_argument("numeric");
                sample.value = value.get<double>();
                break;
            case ValueType::Integer:
                sample.value = integer(value);
                break;
            case ValueType::Boolean:
                sample.value = value.get<bool>();
                break;
            case ValueType::Text:
                sample.value = value.get<std::string>();
                break;
        }
    }
    if (!valid_sample(sample)) throw std::invalid_argument("record");
    return sample;
}

influxdb::Point point(const Sample& sample) {
    influxdb::Point result{measurement(sample.value_type)};
    result.addTag("series_key", sample.series_key)
        .addTag("point_id", hex(sample.point_id))
        .addTag("event_id", hex(sample.event_id))
        .addTag("scope_kind", sample.scope_kind)
        .addTag("scope_id", hex(sample.scope_id));
    const bool present = !std::holds_alternative<std::monostate>(sample.value);
    result.addField("value_present", present)
        .addField("quality", static_cast<int>(sample.quality))
        .addField("source_time_us", static_cast<long long>(sample.source_time_us))
        .addField("ingest_time_us", static_cast<long long>(sample.ingest_time_us))
        .addField("measurement_time_us", static_cast<long long>(sample.measurement_time_us))
        // JSON preserves null, control characters and exact double/int64 round trips.
        .addField("record", record(sample).dump());
    switch (sample.value_type) {
        case ValueType::Numeric:
            result.addField("sample_value", present ? std::get<double>(sample.value) : 0.0);
            break;
        case ValueType::Integer:
            result.addField(
                "sample_value",
                present ? static_cast<long long>(std::get<std::int64_t>(sample.value)) : 0LL);
            break;
        case ValueType::Boolean:
            result.addField("sample_value", present && std::get<bool>(sample.value));
            break;
        case ValueType::Text:
            result.addField("sample_value", present
                                                ? Json(std::get<std::string>(sample.value)).dump()
                                                : std::string("null"));
            break;
    }
    result.setTimestamp(std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(sample.source_time_us))});
    return result;
}

core::Error corrupt() {
    return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                            "invalid InfluxDB history response");
}

std::unique_ptr<influxdb::InfluxDB> client(const InfluxDbConnectionOptions& options,
                                           const core::CallContext& ctx) {
    auto url = options.url;
    while (!url.empty() && url.back() == '/') url.pop_back();
    auto result =
        influxdb::InfluxDBBuilder::http(url + "?db=" + options.database)
            .setAuthToken(options.token)
            .setVerifyCertificate(true)
            .setTimeout(std::chrono::milliseconds(std::max<std::int64_t>(
                1, std::min<std::int64_t>(options.timeout_ms, core::remaining_budget_ms(ctx)))))
            .connect();
    result->setTimePrecision(influxdb::TimePrecision::MicroSeconds);
    return result;
}

bool before(const Sample& left, const Sample& right) {
    return std::tie(left.source_time_us, left.point_id, left.event_id) <
           std::tie(right.source_time_us, right.point_id, right.event_id);
}

core::CallContext request_context(const InfluxDbConnectionOptions& options,
                                  const core::CallContext& ctx) {
    auto result = ctx;
    result.deadline =
        std::min(ctx.deadline, core::make_deadline(std::chrono::milliseconds(options.timeout_ms)));
    return result;
}

class InfluxSeries final : public ITimeSeriesDatabase {
   public:
    explicit InfluxSeries(InfluxDbConnectionOptions options) : options_(std::move(options)) {}

    core::Status write(const Sample& sample, const core::CallContext& ctx) override {
        return write_batch({sample}, ctx).front();
    }

    std::vector<core::Status> write_batch(const std::vector<Sample>& samples,
                                          const core::CallContext& ctx) override {
        const auto reject = [&](core::ErrorCode code, core::WriteEffect effect) {
            return std::vector<core::Status>(
                samples.size(), core::make_error(code, effect, "InfluxDB history batch failed"));
        };
        if (samples.empty()) return {};
        if (samples.size() > 256 || !std::all_of(samples.begin(), samples.end(), valid_sample))
            return reject(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied);
        std::vector<influxdb::Point> points;
        try {
            for (const auto& sample : samples) points.push_back(point(sample));
        } catch (const std::exception&) {
            return reject(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied);
        }
        if (core::remaining_budget_ms(ctx) <= 0)
            return reject(core::ErrorCode::DeadlineExceeded, core::WriteEffect::NotApplied);
        const auto request_ctx = request_context(options_, ctx);
        try {
            // No persistent batch: a failed send cannot leak rows into the next call.
            client(options_, request_ctx)->write(std::move(points));
            return std::vector<core::Status>(samples.size(), core::ok_status());
        } catch (const std::exception&) {
            return reject(core::remaining_budget_ms(request_ctx) <= 0
                              ? core::ErrorCode::DeadlineExceeded
                              : core::ErrorCode::Unavailable,
                          core::WriteEffect::Unknown);
        }
    }

    core::Result<std::vector<Sample>> scan(const HistoryQuery& query,
                                           const std::vector<SeriesMapping>& mappings,
                                           std::uint32_t limit,
                                           const core::CallContext& ctx) override {
        if (core::remaining_budget_ms(ctx) <= 0)
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplicable, "InfluxDB history deadline");
        if (limit == 0 || limit > kMaxRows || mappings.size() > 64 ||
            query.start_us >= query.end_us || query.start_us < -kMaxTimeUs ||
            query.end_us > kMaxTimeUs ||
            (query.scope_kind != "station" && query.scope_kind != "unit") || query.scope_id.empty())
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable,
                                    "InfluxDB history scan bounds");
        std::vector<Sample> samples;
        for (const auto& mapping : mappings) {
            if (!valid_series_key(mapping.series_key) || !measurement(mapping.value_type) ||
                mapping.point_id.empty())
                return corrupt();
            const std::string base =
                "SELECT \"record\",\"source_time_us\",\"event_id\",\"point_id\",\"series_key\","
                "\"scope_kind\",\"scope_id\" FROM \"" +
                std::string(measurement(mapping.value_type)) + "\" WHERE \"series_key\" = '" +
                mapping.series_key + "' AND \"point_id\" = '" + hex(mapping.point_id) +
                "' AND \"scope_kind\" = '" + query.scope_kind + "' AND \"scope_id\" = '" +
                hex(query.scope_id) + "' AND time >= " + std::to_string(query.start_us) +
                "u AND time < " + std::to_string(query.end_us) + "u";
            std::vector<Sample> rows;
            if (query.cursor && query.cursor->source_time_us >= query.start_us &&
                query.cursor->source_time_us < query.end_us) {
                auto equal = fetch(
                    base + " AND time = " + std::to_string(query.cursor->source_time_us) + "u",
                    mapping, query, kMaxRows + 1, ctx);
                if (!core::is_ok(equal)) return core::error_of(equal);
                rows = std::move(std::get<0>(equal));
                if (rows.size() > kMaxRows) return crowded();
                const auto& cursor = *query.cursor;
                rows.erase(std::remove_if(rows.begin(), rows.end(),
                                          [&](const Sample& row) {
                                              return std::tie(row.point_id, row.event_id) <=
                                                     std::tie(cursor.point_id, cursor.event_id);
                                          }),
                           rows.end());
            }
            if (!query.cursor || query.cursor->source_time_us < query.end_us) {
                auto sql = base;
                if (query.cursor && query.cursor->source_time_us >= query.start_us)
                    sql += " AND time > " +
                           std::to_string(std::max(-kMaxTimeUs, query.cursor->source_time_us)) +
                           "u";
                auto later = fetch(sql, mapping, query, limit, ctx);
                if (!core::is_ok(later)) return core::error_of(later);
                auto page = std::move(std::get<0>(later));
                // InfluxQL orders only by time. Fetch the entire boundary timestamp
                // before sorting event IDs, otherwise a cursor could skip equal-time rows.
                if (page.size() == limit) {
                    const auto boundary = page.back().source_time_us;
                    auto ties = fetch(base + " AND time = " + std::to_string(boundary) + "u",
                                      mapping, query, kMaxRows + 1, ctx);
                    if (!core::is_ok(ties)) return core::error_of(ties);
                    auto complete = std::move(std::get<0>(ties));
                    if (complete.size() > kMaxRows) return crowded();
                    page.erase(std::remove_if(page.begin(), page.end(),
                                              [&](const Sample& row) {
                                                  return row.source_time_us == boundary;
                                              }),
                               page.end());
                    page.insert(page.end(), complete.begin(), complete.end());
                }
                rows.insert(rows.end(), page.begin(), page.end());
            }
            std::sort(rows.begin(), rows.end(), before);
            if (rows.size() > limit) rows.resize(limit);
            samples.insert(samples.end(), rows.begin(), rows.end());
        }
        std::sort(samples.begin(), samples.end(), before);
        if (samples.size() > limit) samples.resize(limit);
        return samples;
    }

   private:
    core::Error crowded() const {
        return core::make_error(core::ErrorCode::ResourceExhausted,
                                core::WriteEffect::NotApplicable,
                                "InfluxDB timestamp contains more than 1001 events per point");
    }

    core::Result<std::vector<Sample>> fetch(const std::string& sql, const SeriesMapping& mapping,
                                            const HistoryQuery& query, std::uint32_t limit,
                                            const core::CallContext& ctx) const {
        if (core::remaining_budget_ms(ctx) <= 0)
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplicable, "InfluxDB history deadline");
        std::string response;
        const auto request_ctx = request_context(options_, ctx);
        try {
            response = client(options_, request_ctx)
                           ->execute(sql + " ORDER BY time ASC LIMIT " + std::to_string(limit));
        } catch (const std::exception&) {
            return core::make_error(core::remaining_budget_ms(request_ctx) <= 0
                                        ? core::ErrorCode::DeadlineExceeded
                                        : core::ErrorCode::Unavailable,
                                    core::WriteEffect::NotApplicable, "InfluxDB query failed");
        }
        try {
            const auto root = Json::parse(response);
            const auto& results = root.at("results");
            if (!results.is_array() || results.size() != 1 || results[0].contains("error"))
                return corrupt();
            std::vector<Sample> rows;
            if (!results[0].contains("series")) return rows;
            for (const auto& series : results[0].at("series")) {
                if (series.at("name") != measurement(mapping.value_type)) return corrupt();
                const auto columns = series.at("columns").get<std::vector<std::string>>();
                const auto index = [&](const char* name) {
                    const auto it = std::find(columns.begin(), columns.end(), name);
                    if (it == columns.end()) throw std::invalid_argument("column");
                    return static_cast<std::size_t>(it - columns.begin());
                };
                for (const auto& values : series.at("values")) {
                    if (!values.is_array() || values.size() != columns.size()) return corrupt();
                    auto sample = read_record(values.at(index("record")).get<std::string>());
                    std::istringstream time(values.at(index("time")).get<std::string>());
                    std::chrono::system_clock::time_point timestamp;
                    time >> date::parse("%FT%TZ", timestamp);
                    if (time.fail() || time.peek() != std::char_traits<char>::eof() ||
                        timestamp.time_since_epoch() !=
                            std::chrono::microseconds(sample.source_time_us))
                        return corrupt();
                    if (sample.point_id != mapping.point_id ||
                        sample.series_key != mapping.series_key ||
                        sample.value_type != mapping.value_type ||
                        sample.scope_kind != query.scope_kind ||
                        sample.scope_id != query.scope_id ||
                        sample.source_time_us < query.start_us ||
                        sample.source_time_us >= query.end_us ||
                        integer(values.at(index("source_time_us"))) != sample.source_time_us ||
                        values.at(index("event_id")) != hex(sample.event_id) ||
                        values.at(index("point_id")) != hex(sample.point_id) ||
                        values.at(index("series_key")) != sample.series_key ||
                        values.at(index("scope_kind")) != sample.scope_kind ||
                        values.at(index("scope_id")) != hex(sample.scope_id))
                        return corrupt();
                    rows.push_back(std::move(sample));
                    if (rows.size() > limit) return corrupt();
                }
            }
            if (!std::is_sorted(rows.begin(), rows.end(), [](const Sample& a, const Sample& b) {
                    return a.source_time_us < b.source_time_us;
                }))
                return corrupt();
            return rows;
        } catch (const std::exception&) {
            return corrupt();
        }
    }

    InfluxDbConnectionOptions options_;
};
}  // namespace

core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_influxdb_series(
    const InfluxDbConnectionOptions& options) {
    if (!influx::valid_options(options))
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "invalid InfluxDB connection options");
    try {
        const auto ctx = core::make_call_context(std::chrono::milliseconds(options.timeout_ms));
        const auto probe = Json::parse(client(options, ctx)
                                           ->execute("SELECT \"record\" FROM \"telemetry_numeric\" "
                                                     "WHERE time > now() - 1u LIMIT 1"));
        if (!probe.contains("results") || !probe.at("results").is_array() ||
            probe.at("results").empty() || probe.at("results")[0].contains("error"))
            return core::make_error(core::ErrorCode::SchemaMismatch,
                                    core::WriteEffect::NotApplicable,
                                    "InfluxDB DBRP mapping or query access unavailable");
        return std::unique_ptr<ITimeSeriesDatabase>(new InfluxSeries(options));
    } catch (const std::exception&) {
        return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplicable,
                                "InfluxDB connection failed");
    }
}
}  // namespace ems::database
