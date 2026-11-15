#include "runtime_service.h"

#include <common/core/time.h>
#include <rtdb/client/rtdb.h>

#include <algorithm>
#include <cmath>

#include "web/modules/history/history_service.h"
#include "web/modules/identity/application/authorization_service.h"

namespace ems::web {
namespace {
std::string command_reason(const messaging::ZmqCommandReply& reply) {
    const auto result = Json::parse(reply.reason, nullptr, false);
    return result.is_object() ? result.value("reason", reply.reason) : reply.reason;
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
      history_reader_(std::move(history_reader)) {
#if defined(EMS_WITH_ZEROMQ)
    // 每次提交使用独立 REQ socket，可从业务线程池并发调用。
    if (!options_.command_endpoint.empty()) {
        auto config = std::make_shared<messaging::ZmqCommandConfig>();
        config->endpoint = options_.command_endpoint;
        command_submitter_ = [config](const messaging::ZmqCommandRequest& request) {
            return messaging::ZmqCommandClient(*config).send(request);
        };
    }
#endif
}

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
        fields(body, {"device_id", "point_id", "point_type", "value", "reason", "intent_id",
                      "expires_at_ms"});
        const auto kind = text(body, "point_type", 2);
        if (kind != "yk" && kind != "yt") throw ApiError(422, "InvalidArgument", "command_type");
        request.principal.require(kind == "yk" ? "command.yk" : "command.yt");
        const auto point = request.one(point_table(kind), "point_id", text(body, "point_id"));
        if (point.at("owner_device_id") != owner.at("device_id"))
            throw ApiError(404, "NotFound", "point_not_found");
        if (point.at("enabled") == 0 || owner.at("enabled") == 0)
            throw ApiError(422, "InvalidArgument", "command_disabled");
        const auto& intent = text(body, "intent_id", 128);
        if (!body.at("value").is_number() || !std::isfinite(body.at("value").get<double>()))
            throw ApiError(422, "InvalidArgument", "command_value");
        if (!command_submitter_)
            throw ApiError(503, "NotReady", "control_service_unavailable", "NotApplied");
        const auto now = core::now_utc_micros() / 1000;
        if (body.contains("expires_at_ms") && !body.at("expires_at_ms").is_number_integer())
            throw ApiError(422, "InvalidArgument", "invalid_command_deadline");
        const auto expires_at = body.value("expires_at_ms", now + 3000);
        if (expires_at <= 0 || expires_at > now + 3000)
            throw ApiError(422, "InvalidArgument", "invalid_command_deadline");
        // 采集侧命令契约：载荷仅允许 op/point_id/value/config_revision/expires_at_ms。
        const Json payload = {{"op", "submit"},
                              {"point_id", point.at("point_id")},
                              {"value", body.at("value")},
                              {"config_revision", request.station.at("config_revision")},
                              {"expires_at_ms", expires_at}};
        const auto reply = command_submitter_({intent, payload.dump()});
        using Code = messaging::ZmqCommandReplyCode;
        if (reply.code == Code::Accepted || reply.code == Code::Duplicate ||
            reply.code == Code::Unknown) {
            // Duplicate 表示同 ID 已登记；Unknown 表示不能确认登记，调用方用同一 ID 对账查询。
            return {{{"intent_id", intent}, {"status", "accepted"}}, Json::object(), 202};
        }
        if (reply.code == Code::Rejected)
            throw ApiError(409, "Conflict", "command_rejected", "NotApplied",
                           Json::array({{{"reason", command_reason(reply)}}}));
        if (reply.code == Code::Invalid)
            throw ApiError(422, "InvalidArgument", "command_invalid", "NotApplied",
                           Json::array({{{"reason", command_reason(reply)}}}));
        throw ApiError(503, "NotReady", "collector_not_ready", "NotApplied");
    }
    if (path == "/api/commands/query") {
        fields(body, {"device_id", "intent_id"});
        if (!request.principal.permissions.count("command.yk") &&
            !request.principal.permissions.count("command.yt"))
            throw ApiError(403, "PermissionDenied", "command_read_denied");
        const auto& intent = text(body, "intent_id", 128);
        if (!command_submitter_)
            throw ApiError(503, "NotReady", "control_service_unavailable", "NotApplicable");
        const auto reply = command_submitter_({intent, R"({"op":"query"})"});
        using Code = messaging::ZmqCommandReplyCode;
        if (reply.code == Code::Rejected)
            throw ApiError(404, "NotFound", "intent_not_found", "NotApplicable");
        if (reply.code == Code::RetryLater)
            throw ApiError(503, "NotReady", "collector_not_ready", "NotApplicable");
        if (reply.code == Code::Unknown)
            throw ApiError(503, "NotReady", "collector_unreachable", "Unknown");
        auto row = Json::parse(reply.reason, nullptr, false);
        if (reply.code != Code::Result || row.is_discarded() || !row.is_object() ||
            !row.contains("status"))
            throw ApiError(503, "NotReady", "control_result_corrupt", "NotApplicable");
        // 命令所属点必须属于请求可见的设备，避免越权探测其他设备的意图。
        const auto point_id = row.value("point_id", "");
        bool owned = false;
        for (const auto* kind : {"yk", "yt"}) {
            try {
                owned =
                    request.one(point_table(kind), "point_id", point_id).at("owner_device_id") ==
                    owner.at("device_id");
                break;
            } catch (const ApiError& error) {
                if (error.status != 404) throw;
            }
        }
        if (!owned) throw ApiError(404, "NotFound", "intent_not_found", "NotApplicable");
        return {
            {// 终态：Succeeded/Failed/Expired/Rejected；中间态：Queued/Executing/Acked/Unknown。
             {"intent_id", intent},
             {"status", row.at("status")},
             {"reason", row.value("reason", "")},
             {"updated_at_ms", row.value("updated_at_ms", 0)}}};
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
