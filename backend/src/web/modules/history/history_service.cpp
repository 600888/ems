#include "history_service.h"

#include <common/database/timeseries/factory.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>

namespace ems::web {
namespace {
constexpr std::size_t kScanBudget = 200000;
constexpr std::int64_t kMaxRangeMs = 30LL * 86400000;
constexpr std::int64_t kMaxTimestampMs = 8640000000000000LL;

auto identity(const database::Sample& sample) {
    return std::tie(sample.source_time_us, sample.point_id, sample.event_id);
}

std::int64_t integer(const Json& body, const char* key, std::int64_t maximum,
                     std::int64_t fallback = -1) {
    if (!body.contains(key)) {
        if (fallback >= 0) return fallback;
        throw ApiError(400, "InvalidArgument", "history_bounds");
    }
    const auto& value = body.at(key);
    if (!value.is_number_integer() || value < 0 || value > maximum)
        throw ApiError(400, "InvalidArgument", "history_bounds");
    return value.get<std::int64_t>();
}

HistoryService::Reader deployed_reader(const std::string& config) {
    auto loaded = database::load_time_series_options(config);
    if (!core::is_ok(loaded)) throw ApiError(503, "NotReady", "history_service_unavailable");
    const auto& options = core::value_of(loaded);
    auto connected = database::connect_time_series(options);
    if (!core::is_ok(connected)) throw ApiError(503, "NotReady", "history_service_unavailable");
    std::shared_ptr<database::ITimeSeriesDatabase> series = std::move(std::get<0>(connected));
    return [series](const auto& query, const auto& mappings, auto limit, const auto& ctx) {
        return series->scan(query, mappings, limit, ctx);
    };
}

struct Stream {
    database::HistoryQuery query;
    std::vector<database::Sample> batch;
    std::size_t index{0};
    bool exhausted{false};
};

// 每个作用域持有至多 1001 行，归并前校验范围、映射与严格递增游标。
bool available(Stream& stream, const HistoryService::Reader& reader,
               const std::vector<database::SeriesMapping>& mappings, const core::CallContext& ctx,
               std::uint32_t batch_limit) {
    if (stream.index < stream.batch.size()) return true;
    if (stream.exhausted) return false;
    if (core::has_expired(ctx))
        fail(core::make_error(core::ErrorCode::DeadlineExceeded, core::WriteEffect::NotApplicable));
    auto batch = take(reader(stream.query, mappings, batch_limit, ctx));
    if (batch.size() > batch_limit) throw ApiError(503, "CorruptData", "history_bounds");
    for (std::size_t i = 0; i < batch.size(); ++i) {
        const auto& sample = batch[i];
        const auto mapping = std::find_if(mappings.begin(), mappings.end(), [&](const auto& m) {
            return m.point_id == sample.point_id;
        });
        const auto& q = stream.query;
        if (sample.scope_kind != q.scope_kind || sample.scope_id != q.scope_id ||
            sample.source_time_us < q.start_us || sample.source_time_us >= q.end_us ||
            mapping == mappings.end() || sample.series_key != mapping->series_key ||
            sample.value_type != mapping->value_type || sample.event_id.empty())
            throw ApiError(503, "CorruptData", "history_scope_mismatch");
        if ((q.cursor && identity(sample) <= std::tie(q.cursor->source_time_us, q.cursor->point_id,
                                                      q.cursor->event_id)) ||
            (i && identity(sample) <= identity(batch[i - 1])))
            throw ApiError(503, "CorruptData", "history_order_mismatch");
        const auto number = std::get_if<double>(&sample.value);
        if (number && !std::isfinite(*number))
            throw ApiError(503, "CorruptData", "invalid_history_sample");
    }
    stream.exhausted = batch.size() < batch_limit;
    if (!batch.empty()) {
        const auto& last = batch.back();
        database::QueryCursor cursor;
        cursor.source_time_us = last.source_time_us;
        cursor.point_id = last.point_id;
        cursor.event_id = last.event_id;
        stream.query.cursor = std::move(cursor);
    }
    stream.batch = std::move(batch);
    stream.index = 0;
    return !stream.batch.empty();
}

Json encode(const database::Sample& sample, std::int64_t timestamp) {
    Json value;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, std::monostate>) value = v;
        },
        sample.value);
    return {{"point_id", sample.point_id},
            {"timestamp", timestamp},
            {"value", value},
            {"quality_code", sample.quality}};
}
}  // namespace

HistoryService::HistoryService(database::MySqlDatabaseContext database, Reader reader,
                               std::string timeseries_config)
    : database_(std::move(database)),
      reader_(std::move(reader)),
      timeseries_config_(std::move(timeseries_config)) {}

DeviceResult HistoryService::execute(const Json& body, const Principal& actor,
                                     const core::CallContext& ctx) {
    fields(body, {"device_id", "point_type", "point_ids", "start_ms", "end_ms", "max_rows",
                  "interval_ms"});
    const auto kind = body.contains("point_type") ? text(body, "point_type", 2) : "yc";
    if (kind != "yc" && kind != "yx") throw ApiError(400, "InvalidArgument", "history_point_type");
    const auto start = integer(body, "start_ms", kMaxTimestampMs);
    const auto end = integer(body, "end_ms", kMaxTimestampMs);
    const auto limit = integer(body, "max_rows", 10000, 1000);
    const auto interval = integer(body, "interval_ms", 3600000, 0);
    const std::set<std::int64_t> intervals{0, 60000, 300000, 900000, 3600000};
    if (end <= start || end - start > kMaxRangeMs || limit < 1 || !intervals.count(interval) ||
        !body.contains("point_ids") || !body.at("point_ids").is_array() ||
        body.at("point_ids").empty() || body.at("point_ids").size() > 6)
        throw ApiError(400, "InvalidArgument", "history_bounds");
    std::vector<std::string> ids;
    for (const auto& id : body.at("point_ids")) ids.push_back(text(Json{{"id", id}}, "id"));
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    DeviceRequest request(database_, actor, ctx, false);
    request.principal.require("history.read");
    const auto owner = request.device(text(body, "device_id"));
    if (!database_.history_catalog) throw ApiError(503, "NotReady", "history_service_unavailable");
    std::vector<database::SeriesMapping> mappings;
    for (const auto& id : ids) {
        const auto point = request.one(point_table(kind), "point_id", id);
        if (point.at("owner_device_id") != owner.at("device_id"))
            throw ApiError(404, "NotFound", "point_not_found");
        auto result = database_.history_catalog->find_series(*request.transaction, id, ctx);
        if (!core::is_ok(result) && core::error_of(result).code == core::ErrorCode::NotFound)
            continue;
        auto mapping = take(std::move(result));
        if (mapping.point_id != id || mapping.series_key.empty())
            throw ApiError(503, "CorruptData", "history_scope_mismatch");
        mappings.push_back(std::move(mapping));
    }
    std::vector<std::pair<std::string, std::string>> scopes{
        {"station", request.station.at("device_id").get<std::string>()}};
    auto ancestor = owner;
    std::set<std::string> visited;
    while (ancestor.at("node_kind") != "station") {
        const auto id = ancestor.at("device_id").get<std::string>();
        if (!visited.insert(id).second)
            throw ApiError(503, "CorruptData", "device_hierarchy_cycle");
        if (ancestor.at("node_kind") == "unit") {
            scopes.emplace_back("unit", id);
            break;
        }
        ancestor = request.one(database::DeviceTable::Device, "device_id",
                               ancestor.at("parent_device_id"));
    }
    if (mappings.empty()) return request.finish({{{"items", Json::array()}, {"truncated", false}}});
    request.finish({Json::object()});
    request.transaction.reset();
    const auto reader = reader_ ? reader_ : deployed_reader(timeseries_config_);
    std::vector<Stream> streams;
    for (const auto& [scope_kind, scope_id] : scopes) {
        Stream stream;
        stream.query.scope_kind = scope_kind;
        stream.query.scope_id = scope_id;
        stream.query.point_ids = ids;
        stream.query.start_us = start * 1000;
        stream.query.end_us = end * 1000;
        stream.query.max_rows = static_cast<std::uint32_t>(limit);
        streams.push_back(std::move(stream));
    }
    Json items = Json::array();
    std::map<std::string, database::Sample> bucket;
    std::int64_t bucket_time = -1;
    std::optional<database::Sample> previous;
    bool truncated = false;
    std::size_t scanned = 0;
    const auto flush = [&] {
        for (const auto& [id, sample] : bucket) items.push_back(encode(sample, bucket_time));
        bucket.clear();
    };
    const auto batch_limit =
        static_cast<std::uint32_t>(interval ? 1001 : std::min<std::int64_t>(1001, limit + 1));
    for (;;) {
        Stream* next = nullptr;
        for (auto& stream : streams) {
            if (available(stream, reader, mappings, ctx, batch_limit) &&
                (!next ||
                 identity(stream.batch[stream.index]) < identity(next->batch[next->index])))
                next = &stream;
        }
        if (!next) {
            flush();
            break;
        }
        if (scanned++ == kScanBudget) {
            truncated = true;
            break;  // 不输出尚未完成扫描的时间桶。
        }
        const auto sample = next->batch[next->index++];
        if (previous && identity(*previous) == identity(sample)) continue;
        previous = sample;
        const auto timestamp = sample.source_time_us / 1000;
        if (interval == 0) {
            items.push_back(encode(sample, timestamp));
        } else {
            const auto time = start + ((timestamp - start) / interval) * interval;
            if (bucket_time != time) {
                flush();
                bucket_time = time;
            }
            // 时间桶保留最后一个实际样本（含质量码），不填充无数据的桶。
            bucket[sample.point_id] = sample;
        }
        if (items.size() > static_cast<std::size_t>(limit)) break;
    }
    truncated = truncated || items.size() > static_cast<std::size_t>(limit);
    if (truncated && items.size() > static_cast<std::size_t>(limit))
        items.erase(items.begin() + limit, items.end());
    // 时序读取不持有配置锁；返回前重新验证权限和点位归属，避免读取期间撤权泄漏。
    DeviceRequest completion(database_, actor, ctx, false);
    completion.principal.require("history.read");
    completion.device(text(body, "device_id"));
    for (const auto& id : ids) {
        const auto point = completion.one(point_table(kind), "point_id", id);
        if (point.at("owner_device_id") != owner.at("device_id"))
            throw ApiError(404, "NotFound", "point_not_found");
    }
    return completion.finish({{{"items", items}, {"truncated", truncated}}});
}
}  // namespace ems::web
