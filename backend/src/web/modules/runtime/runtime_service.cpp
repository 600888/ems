#include "runtime_service.h"

#include <common/core/time.h>
#include <rtdb/client/rtdb.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <tuple>

#include "web/modules/identity/application/authorization_service.h"

namespace ems::web {
namespace {
std::string environment(const char* key, std::string fallback = {}) {
    auto value = std::getenv(key);
    return value ? value : fallback;
}

Json point_sample(const Json& point, const std::string& kind,
                  const RuntimeService::ReadPoint& reader, const core::CallContext& ctx,
                  bool device_enabled, int default_stale_ms) {
    Json result = {{"definition", public_row(point)}, {"sample", nullptr}, {"quality", "missing"}};
    if (point.at("enabled") == 0 || !device_enabled) {
        result["quality"] = "disabled";
        return result;
    }
    auto read = reader(kind, point.at("point_id"), ctx);
    if (!core::is_ok(read)) {
        if (core::error_of(read).code != core::ErrorCode::NotFound) fail(core::error_of(read));
        return result;
    }
    const auto sample = Json::parse(core::value_of(read), nullptr, false);
    if (sample.is_discarded()) throw ApiError(503, "CorruptData", "invalid_realtime_sample");
    if (!sample.contains("value") || !sample.contains("timestamp") || !sample.contains("quality"))
        throw ApiError(503, "CorruptData", "invalid_realtime_sample");
    const auto now = core::now_utc_micros() / 1000;
    const auto timestamp = sample.at("timestamp").get<std::int64_t>();
    const auto configured_stale = point.value("stale_after_ms", 60000);
    const auto stale_ms = configured_stale > 0 ? configured_stale : default_stale_ms;
    const bool stale =
        timestamp <= 0 || timestamp > now + 300000 || (stale_ms > 0 && now - timestamp > stale_ms);
    result["sample"] = {{"value", sample.at("value")},
                        {"timestamp", timestamp},
                        {"quality_code", sample.at("quality")}};
    result["quality"] = stale ? "stale" : sample.at("quality") == 0 ? "good" : "invalid";
    return result;
}
}  // namespace

RuntimeService::RuntimeService(database::MySqlDatabaseContext database, RuntimeOptions options,
                               ReadPoint reader, ReadHistory history_reader)
    : database_(std::move(database)),
      options_(std::move(options)),
      reader_(std::move(reader)),
      history_reader_(std::move(history_reader)) {}

DeviceResult RuntimeService::execute(const std::string& path, const Json& body,
                                     const Principal& actor, const core::CallContext& ctx) {
    if (path == "/api/audit/list") {
        fields(body, {"page", "page_size"});
        auto tx = take(database_.database->begin(ctx));
        auto principal = AuthorizationService::recheck(database_, *tx, actor, ctx);
        principal.require("audit.read");
        const int page = body.value("page", 1), size = body.value("page_size", 20);
        if (page < 1 || page > 10000 || size < 1 || size > 100)
            throw ApiError(400, "InvalidArgument", "pagination_bounds");
        if (!database_.audit) throw ApiError(503, "NotReady", "audit_unavailable");
        const auto rows = take(database_.audit->list(*tx, (page - 1) * size, size + 1, ctx));
        Json items = Json::array();
        for (const auto& row : rows) {
            if (items.size() == static_cast<std::size_t>(size)) break;
            items.push_back({{"event_id", row.event_id},
                             {"action", row.action},
                             {"actor", row.actor_user_id.value_or("")},
                             {"target", row.resource_id.value_or("")},
                             {"outcome", row.outcome},
                             {"reason", row.reason.value_or("")},
                             {"occurred_at", row.occurred_at},
                             {"request_id", row.request_id}});
        }
        AuthorizationService::recheck(database_, *tx, actor, ctx);
        take(tx->commit(ctx));
        return {{{"items", items}, {"has_more", rows.size() > static_cast<std::size_t>(size)}}};
    }
    DeviceRequest request(database_, actor, ctx, false);
    const auto owner = request.device(text(body, "device_id"));
    if (path == "/api/commands/submit") {
        fields(body, {"device_id", "point_id", "point_type", "value", "reason", "intent_id"});
        const auto kind = text(body, "point_type", 2);
        if (kind != "yk" && kind != "yt") throw ApiError(422, "InvalidArgument", "command_type");
        request.principal.require(kind == "yk" ? "command.yk" : "command.yt");
        const auto point = request.one(point_table(kind), "point_id", text(body, "point_id"));
        if (point.at("owner_device_id") != owner.at("device_id"))
            throw ApiError(404, "NotFound", "point_not_found");
        if (point.at("enabled") == 0 || owner.at("enabled") == 0)
            throw ApiError(422, "InvalidArgument", "command_disabled");
        // Web 不绕过策略仲裁直接写采集通道；当前没有可持久受理意图的策略服务。
        throw ApiError(503, "NotReady", "control_service_unavailable", "NotApplied");
    }
    if (path == "/api/history/query") {
        request.principal.require("history.read");
        fields(body, {"device_id", "point_ids", "start_ms", "end_ms", "max_rows"});
        auto ids = body.at("point_ids").get<std::vector<std::string>>();
        const auto start = body.at("start_ms").get<std::int64_t>();
        const auto end = body.at("end_ms").get<std::int64_t>();
        const int limit = body.value("max_rows", 1000);
        if (ids.empty() || ids.size() > 6 || start < 0 || end <= start ||
            end > std::numeric_limits<std::int64_t>::max() / 1000 || end - start > 86400000 ||
            limit < 1 || limit > 10000)
            throw ApiError(400, "InvalidArgument", "history_bounds");
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        if (!database_.history_catalog)
            throw ApiError(503, "NotReady", "history_service_unavailable");
        std::vector<database::SeriesMapping> mappings;
        for (const auto& id : ids) {
            const auto point = request.one(database::DeviceTable::Yc, "point_id", id);
            if (point.at("owner_device_id") != owner.at("device_id"))
                throw ApiError(404, "NotFound", "point_not_found");
            auto mapping = database_.history_catalog->find_series(*request.transaction, id, ctx);
            if (!core::is_ok(mapping) && core::error_of(mapping).code == core::ErrorCode::NotFound)
                continue;
            mappings.push_back(take(std::move(mapping)));
        }
        const auto station = request.station.at("device_id").get<std::string>();
        std::vector<std::pair<std::string, std::string>> scopes = {{"station", station}};
        // 存储按事件所属站或单元落库；只从已授权设备的真实祖先推导查询作用域。
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
        if (mappings.empty())
            return request.finish({{{"items", Json::array()}, {"truncated", false}}});
        request.finish({Json::object()});
        request.transaction.reset();
        auto read_history = history_reader_;
        if (!read_history) {
            database::TimeSeriesConnectionOptions options;
            options.taos.host = environment("EMS_TAOS_HOST", "127.0.0.1");
            options.taos.user = environment("EMS_TAOS_USER");
            options.taos.password = environment("EMS_TAOS_PASSWORD");
            options.taos.database = environment("EMS_TAOS_DATABASE", "ems_history");
            if (options.taos.user.empty())
                throw ApiError(503, "NotReady", "history_service_unavailable");
            const auto parsed_port = std::stoul(environment("EMS_TAOS_PORT", "6030"));
            if (parsed_port < 1 || parsed_port > 65535)
                throw ApiError(503, "NotReady", "history_service_unavailable");
            options.taos.port = static_cast<std::uint16_t>(parsed_port);
            auto connected = database::connect_time_series(options);
            if (!core::is_ok(connected))
                throw ApiError(503, "NotReady", "history_service_unavailable");
            std::shared_ptr<database::ITimeSeriesDatabase> series =
                std::move(std::get<0>(connected));
            read_history = [series](const auto& query, const auto& mappings, auto count,
                                    const auto& context) {
                return series->scan(query, mappings, count, context);
            };
        }
        database::HistoryQuery query;
        query.point_ids = ids;
        query.start_us = start * 1000;
        query.end_us = end * 1000;
        query.max_rows = limit;
        std::vector<database::Sample> samples;
        auto identity = [](const auto& sample) {
            return std::tie(sample.source_time_us, sample.point_id, sample.event_id);
        };
        for (const auto& scope : scopes) {
            query.scope_kind = scope.first;
            query.scope_id = scope.second;
            query.cursor.reset();
            std::size_t count = 0;
            // 底层每次最多 1001 行，使用游标有界读取，避免大查询被底层拒绝。
            while (count <= static_cast<std::size_t>(limit)) {
                const auto batch_limit =
                    static_cast<std::uint32_t>(std::min<std::size_t>(1001, limit + 1 - count));
                const auto batch = take(read_history(query, mappings, batch_limit, ctx));
                if (batch.size() > batch_limit)
                    throw ApiError(503, "CorruptData", "history_bounds");
                for (const auto& sample : batch) {
                    const auto mapping =
                        std::find_if(mappings.begin(), mappings.end(),
                                     [&](const auto& m) { return m.point_id == sample.point_id; });
                    if (sample.scope_kind != scope.first || sample.scope_id != scope.second ||
                        sample.source_time_us < query.start_us ||
                        sample.source_time_us >= query.end_us || mapping == mappings.end() ||
                        sample.series_key != mapping->series_key ||
                        sample.value_type != mapping->value_type || sample.event_id.empty() ||
                        (query.cursor && identity(sample) <= std::tie(query.cursor->source_time_us,
                                                                      query.cursor->point_id,
                                                                      query.cursor->event_id)))
                        throw ApiError(503, "CorruptData", "history_scope_mismatch");
                    if (const auto number = std::get_if<double>(&sample.value);
                        number && !std::isfinite(*number))
                        throw ApiError(503, "CorruptData", "invalid_history_sample");
                    if (count && identity(sample) <= identity(samples.back()))
                        throw ApiError(503, "CorruptData", "history_order_mismatch");
                    samples.push_back(sample);
                    ++count;
                }
                if (batch.size() < batch_limit) break;
                const auto& last = batch.back();
                database::QueryCursor cursor;
                cursor.source_time_us = last.source_time_us;
                cursor.point_id = last.point_id;
                cursor.event_id = last.event_id;
                query.cursor = cursor;
            }
        }
        std::sort(samples.begin(), samples.end(),
                  [&](const auto& a, const auto& b) { return identity(a) < identity(b); });
        samples.erase(
            std::unique(samples.begin(), samples.end(),
                        [&](const auto& a, const auto& b) { return identity(a) == identity(b); }),
            samples.end());
        Json items = Json::array();
        for (const auto& sample : samples) {
            if (items.size() == static_cast<std::size_t>(limit)) break;
            Json value;
            std::visit(
                [&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (!std::is_same_v<T, std::monostate>) value = v;
                },
                sample.value);
            items.push_back({{"point_id", sample.point_id},
                             {"timestamp", sample.source_time_us / 1000},
                             {"value", value},
                             {"quality_code", sample.quality}});
        }
        auto tx = take(database_.database->begin(ctx));
        AuthorizationService::recheck(database_, *tx, actor, ctx);
        take(tx->rollback(ctx));
        return {
            {{"items", items}, {"truncated", samples.size() > static_cast<std::size_t>(limit)}}};
    }
    if (path != "/api/monitor/points") throw ApiError(404, "NotFound", "route_not_found");
    request.principal.require("monitor.read");
    fields(body, {"device_id", "point_type", "page", "page_size"});
    const auto kind = text(body, "point_type", 2);
    auto q = page_query(point_table(kind), body, {});
    q.filters = {{"owner_device_id", owner.at("device_id")}};
    auto result = request.query(q);
    const bool device_enabled = owner.at("enabled").get<int>() != 0;
    const bool needs_samples =
        device_enabled && std::any_of(result.at("items").begin(), result.at("items").end(),
                                      [](const auto& point) { return point.at("enabled") != 0; });
    auto reader = reader_;
    std::shared_ptr<rtdb::RtDb> realtime;
    if (!reader && needs_samples) {
        realtime = std::make_shared<rtdb::RtDb>();
        rtdb::RtDbConfig options;
        options.config_path = options_.realtime_config;
        options.scope_code = options_.scope_code;
        if (options.config_path.empty() || !realtime->initialize(options))
            throw ApiError(503, "NotReady", "realtime_service_unavailable");
        reader = [realtime](const auto& kind, const auto& id, const auto& context) {
            return realtime->readPointJson(kind, id, context);
        };
    }
    Json items = Json::array();
    for (const auto& point : result.at("items"))
        items.push_back(point_sample(point, kind, reader, ctx, device_enabled,
                                     request.station.value("default_stale_after_ms", 60000)));
    result["items"] = std::move(items);
    result["page"] = body.value("page", 1);
    result["page_size"] = q.limit;
    return request.finish({result});
}
}  // namespace ems::web
