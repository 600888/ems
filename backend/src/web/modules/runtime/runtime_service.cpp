#include "runtime_service.h"

#include <common/core/time.h>
#include <rtdb/client/rtdb.h>

#include <algorithm>
#include <cmath>

#include "web/modules/history/history_service.h"
#include "web/modules/identity/application/authorization_service.h"

namespace ems::web {
namespace {
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
    if (path == "/api/history/query")
        return HistoryService(database_, history_reader_, options_.timeseries_config)
            .execute(body, actor, ctx);
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
