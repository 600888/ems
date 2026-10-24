#include <set>

#include "device_service.h"

namespace ems::web {
using namespace database;

namespace {
Json point_fields(DeviceTable table, Json current, const Json& patch, bool create) {
    if (!create && patch.count("code"))
        throw ApiError(400, "InvalidArgument", "point_code_readonly");
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        auto key = it.key();
        if (key == "address") continue;
        if (key == "enum_map") key = "enum_json";
        if (key == "feedback") key = "feedback_json";
        if (!current.count(key)) throw ApiError(422, "InvalidArgument", "point_type_mismatch");
    }
    current = merge_fields(current, patch,
                           {"binding_id",
                            "code",
                            "name",
                            "semantic_code",
                            "vendor_key",
                            "origin",
                            "address",
                            "wire_type",
                            "byte_order",
                            "value_type",
                            "unit",
                            "enabled",
                            "disabled_reason",
                            "stale_after_ms",
                            "description",
                            "decimals",
                            "scale_factor",
                            "offset_value",
                            "range_min",
                            "range_max",
                            "deadband",
                            "soe",
                            "invert",
                            "enum_map",
                            "unknown_policy",
                            "feedback_of_point_id",
                            "write_mode",
                            "fixed_raw_value",
                            "pulse_width_ms",
                            "min_interval_ms",
                            "write_group_key",
                            "feedback",
                            "feedback_timeout_ms",
                            "failure_policy"});
    for (auto key : {"binding_id", "code", "name", "wire_type", "value_type"})
        text(current, key, key == std::string("name") ? 128 : 64);

    // 分类和值类型一起检查，不能通过通用字段集合混入另一类点的行为。
    const auto wire = current.at("wire_type").get<std::string>();
    if (wire != "bool" && wire != "i16" && wire != "u16")
        throw ApiError(422, "Unsupported", "wire_layout_unsupported");
    const auto value = current.at("value_type").get<std::string>();
    const bool valid =
        (table == DeviceTable::Yc &&
         (value == "double" || value == "int64" || value == "uint64")) ||
        (table == DeviceTable::Yx && (value == "bool" || value == "int64" || value == "string")) ||
        (table == DeviceTable::Yk && (value == "bool" || value == "int64")) ||
        (table == DeviceTable::Yt && (value == "double" || value == "int64"));
    if (!valid) throw ApiError(422, "InvalidArgument", "point_value_type_mismatch");
    if (current.at("enabled") == 0 && current.at("disabled_reason") == "")
        throw ApiError(422, "InvalidArgument", "disabled_reason_required");
    if (current.count("feedback_json")) {
        auto feedback = Json::parse(current.at("feedback_json").get<std::string>());
        if (!feedback.is_array() || feedback.size() > 32)
            throw ApiError(422, "InvalidArgument", "invalid_feedback");
        for (const auto& item : feedback) {
            fields(item, {"point_code", "criterion", "expected", "tolerance", "order", "required"});
            text(item, "point_code");
            auto criterion = text(item, "criterion");
            if (criterion != "equals" && criterion != "within_tolerance")
                throw ApiError(422, "Unsupported", "feedback_criterion_unsupported");
        }
    }
    return current;
}

Json defaults(DeviceTable table, const std::string& id, const std::string& owner) {
    Json row;
    switch (table) {
        case DeviceTable::Yc:
            row = encode_record(CfgPointYcRow{});
            break;
        case DeviceTable::Yx:
            row = encode_record(CfgPointYxRow{});
            row["unknown_policy"] = "retain_raw";
            break;
        case DeviceTable::Yk:
            row = encode_record(CfgPointYkRow{});
            row["write_mode"] = "direct";
            row["feedback_json"] = "[]";
            row["failure_policy"] = "reject";
            break;
        default:
            row = encode_record(CfgPointYtRow{});
            row["feedback_json"] = "[]";
            row["failure_policy"] = "reject";
            break;
    }
    row["point_id"] = id;
    row["owner_device_id"] = owner;
    row["enabled"] = 0;
    row["origin"] = "vendor";
    return row;
}

void check_binding(DeviceRequest& request, const Json& point, const Json& owner) {
    auto binding = request.one(DeviceTable::Binding, "binding_id", point.at("binding_id"));
    auto source =
        owner.at("node_kind") == "component" ? owner.at("source_device_id") : owner.at("device_id");
    request.device(source);
    if (binding.at("device_id") != source)
        throw ApiError(422, "InvalidArgument", "point_source_mismatch");
}

void remove_point(DeviceRequest& request, DeviceTable table, const std::string& id,
                  const std::string& owner) {
    auto old = request.one(table, "point_id", id);
    if (old.at("owner_device_id") != owner) throw ApiError(404, "NotFound", "point_not_found");

    // 所有反馈引用都需显式解除，不依赖外键级联悄悄删除。
    DeviceQuery q;
    q.table = DeviceTable::Yx;
    q.filters = {{"feedback_of_point_id", id}};
    q.limit = 100000;
    for (const auto& feedback : request.rows(q)) {
        if (!request.deleting_points.count(feedback.at("point_id")))
            throw ApiError(409, "Conflict", "point_referenced_by_feedback");
    }
    for (auto kind : {DeviceTable::Yk, DeviceTable::Yt}) {
        q.table = kind;
        q.filters = {{"owner_device_id", owner}};
        q.limit = 100000;
        for (const auto& command : request.rows(q)) {
            if (request.deleting_points.count(command.at("point_id"))) continue;
            auto payload = command.at("feedback_json").get<std::string>();
            auto feedback = payload.empty() ? Json::array() : Json::parse(payload);
            for (const auto& item : feedback)
                if (item.value("point_code", std::string{}) == old.at("code"))
                    throw ApiError(409, "Conflict", "point_referenced_by_feedback");
        }
    }
    take(request.database.device_config->remove_point(
        *request.transaction,
        static_cast<PointTableKind>(static_cast<int>(table) - static_cast<int>(DeviceTable::Yc)),
        id, request.context));
    request.changed = true;
}
}  // namespace

DeviceResult point_action(DeviceRequest& request, const std::string& path, const Json& body) {
    request.principal.require("device.read");
    auto owner = request.device(text(body, "device_id"));
    const auto action = path.substr(path.find_last_of('/') + 1);
    auto owner_id = owner.at("device_id").get<std::string>();
    if (owner.at("node_kind") != "device" && owner.at("node_kind") != "component")
        throw ApiError(422, "InvalidArgument", "point_owner_invalid");

    if (path == "/api/inherent-points/list") {
        fields(body, {"device_id", "filters", "page", "page_size"});
        auto filter = body.value("filters", Json::object());
        fields(filter, {"point_type", "state", "keyword"});
        DeviceQuery q;
        q.table = DeviceTable::TypePoint;
        q.filters = {{"type_code", owner.at("device_type_code")},
                     {"type_version", owner.at("type_version")}};
        q.limit = 100000;
        auto definitions = request.rows(q);
        Json points = Json::object();
        Json counts = {{"yc", 0}, {"yx", 0}, {"yk", 0}, {"yt", 0}};
        for (auto kind : {"yc", "yx", "yk", "yt"}) {
            q.table = point_table(kind);
            q.filters = {{"owner_device_id", owner_id}};
            for (const auto& row : request.rows(q))
                if (row.at("semantic_code") != "")
                    points[row.at("semantic_code").get<std::string>()] = row;
        }
        auto paging_body = body;
        paging_body.erase("filters");
        const auto paging = page_query(DeviceTable::TypePoint, paging_body, {});
        Json items = Json::array();
        std::size_t total = 0;
        for (auto row : definitions) {
            std::string code = row.at("semantic_code");
            std::string kind = row.at("point_type");
            counts[kind] = counts[kind].get<int>() + 1;
            const auto state =
                !points.count(code)
                    ? "unconfigured"
                    : (points.at(code).at("enabled").get<int>() ? "enabled" : "disabled");
            if (filter.count("point_type") && filter.at("point_type") != kind) continue;
            if (filter.count("state") && filter.at("state") != state) continue;
            auto keyword = filter.value("keyword", std::string{});
            if (!keyword.empty() && code.find(keyword) == std::string::npos &&
                row.at("name").get<std::string>().find(keyword) == std::string::npos)
                continue;
            if (total++ < paging.offset || items.size() >= paging.limit) continue;
            row = public_row(row);
            row["state"] = state;
            row["point"] = points.count(code) ? public_row(points.at(code)) : Json(nullptr);
            items.push_back(row);
        }
        return {{{"items", items},
                 {"total", total},
                 {"counts", counts},
                 {"page", body.value("page", 1)},
                 {"page_size", paging.limit}}};
    }

    if (action == "batch-change") {
        fields(body, {"device_id", "expected_config_revision", "changes"});
        request.principal.require("point.write");
        request.expect_revision(body);
        auto changes = body.at("changes");
        if (!changes.is_array() || changes.size() > 200)
            throw ApiError(422, "InvalidArgument", "batch_size");
        std::set<std::string> ids;
        Json changed = Json::array();
        for (const auto& change : changes) {
            if (change.value("operation", std::string{}) == "delete")
                request.deleting_points.insert(text(change, "point_id"));
        }
        // 先暂存增改，再删除本批集合；相互引用的删除在最终候选工程中统一检查。
        std::stable_sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
            return (a.at("operation") == "delete") < (b.at("operation") == "delete");
        });
        for (const auto& change : changes) {
            fields(change, {"operation", "point_type", "point_id", "fields"});
            auto operation = text(change, "operation");
            if (operation != "create" && operation != "update" && operation != "delete")
                throw ApiError(422, "InvalidArgument", "invalid_point_operation");
            auto id = text(change, "point_id");
            if (!ids.insert(id).second)
                throw ApiError(422, "InvalidArgument", "duplicate_batch_point");
            Json next = {{"device_id", owner_id},
                         {"point_type", change.at("point_type")},
                         {"point_id", id},
                         {"expected_config_revision", body.at("expected_config_revision")}};
            if (change.count("fields")) next["fields"] = change.at("fields");
            point_action(request, "/api/points/" + operation, next);
            changed.push_back(id);
        }
        return {{{"changed_point_ids", changed}}};
    }

    auto kind = text(body, "point_type", 2);
    auto table = point_table(kind);
    if (action == "list") {
        fields(body, {"device_id", "point_type", "filters", "page", "page_size"});
        auto q = page_query(table, body,
                            {"keyword", "enabled", "semantic_code", "binding_id", "origin", "soe"});
        q.filters.push_back({"owner_device_id", owner_id});
        auto result = request.query(q);
        for (auto& row : result["items"]) row = public_row(row);
        result["page"] = body.value("page", 1);
        result["page_size"] = q.limit;
        return {result};
    }
    auto id = text(body, "point_id");
    if (action == "detail") {
        fields(body, {"device_id", "point_type", "point_id"});
        auto row = request.one(table, "point_id", id);
        if (row.at("owner_device_id") != owner_id)
            throw ApiError(404, "NotFound", "point_not_found");
        return {public_row(row)};
    }

    request.principal.require("point.write");
    fields(body, {"device_id", "point_type", "point_id", "expected_config_revision", "fields"});
    if (action == "create") {
        auto row = point_fields(table, defaults(table, id, owner_id), body.at("fields"), true);
        for (auto other : {DeviceTable::Yc, DeviceTable::Yx, DeviceTable::Yk, DeviceTable::Yt}) {
            DeviceQuery q;
            q.table = other;
            q.filters = {{"point_id", id}};
            q.limit = 1;
            auto existing = request.rows(q);
            if (!existing.empty()) {
                request.device(existing.front().at("owner_device_id"));
                if (other != table || !same_record(existing.front(), row))
                    throw ApiError(409, "Conflict", "point_id_conflict");
                DeviceResult replay{public_row(existing.front())};
                replay.meta["replayed"] = true;
                return replay;
            }
        }
        request.expect_revision(body);
        check_binding(request, row, owner);
        request.put(table, row);
        return {public_row(row), Json::object(), 201};
    }
    request.expect_revision(body);
    auto old = request.one(table, "point_id", id);
    if (old.at("owner_device_id") != owner_id) throw ApiError(404, "NotFound", "point_not_found");
    if (action == "update") {
        auto row = point_fields(table, old, body.at("fields"), false);
        check_binding(request, row, owner);
        if (row != old) request.put(table, row);
        return {public_row(row)};
    }
    if (action == "delete") {
        remove_point(request, table, id, owner_id);
        return {{{"removed_id", id}}};
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
