#include <algorithm>
#include <set>

#include "device_service.h"

namespace ems::web {
using namespace database;

namespace {
Json summary(DeviceRequest& request, Json row) {
    auto id = row.at("device_id").get<std::string>();
    DeviceQuery types;
    types.table = DeviceTable::TypePoint;
    types.filters = {{"type_code", row.at("device_type_code")},
                     {"type_version", row.at("type_version")}};
    types.limit = 100000;
    auto definitions = request.query(types).at("items");
    int enabled = 0, disabled = 0;

    for (auto table : {DeviceTable::Yc, DeviceTable::Yx, DeviceTable::Yk, DeviceTable::Yt}) {
        DeviceQuery q;
        q.table = table;
        q.filters = {{"owner_device_id", id}};
        q.limit = 100000;
        for (const auto& point : request.rows(q)) {
            if (point.at("semantic_code") == "") continue;
            if (point.at("enabled").get<int>())
                ++enabled;
            else
                ++disabled;
        }
    }
    const int total = static_cast<int>(definitions.size());
    row = public_row(std::move(row));
    row["inherent_points"] = {{"total", total},
                              {"enabled", enabled},
                              {"disabled", disabled},
                              {"unconfigured", std::max(0, total - enabled - disabled)}};
    row["coverage_ratio"] = total ? Json(static_cast<double>(enabled) / total) : Json(nullptr);
    row["config_completeness"] = (enabled + disabled >= total) ? "complete" : "incomplete";
    return row;
}
}  // namespace

DeviceResult device_action(DeviceRequest& request, const std::string& path, const Json& body) {
    request.principal.require("device.read");
    const auto action = path.substr(path.find_last_of('/') + 1);
    if (action == "configuration") {
        fields(body, {});
        return {{{"station_device_id", request.station.at("device_id")},
                 {"config_revision",
                  std::to_string(request.station.at("config_revision").get<std::int64_t>())}}};
    }

    if (action == "tree" || action == "list") {
        if (action == "tree")
            fields(body, {"parent_device_id", "page", "page_size"});
        else
            fields(body, {"filters", "page", "page_size", "sort", "order"});
        auto normalized = body;
        auto filter = normalized.value("filters", Json::object());
        bool recursive = false;
        std::string completeness;
        if (filter.count("recursive")) {
            recursive = filter.at("recursive").get<bool>();
            filter.erase("recursive");
        }
        if (filter.count("config_completeness")) {
            completeness = filter.at("config_completeness").get<std::string>();
            filter.erase("config_completeness");
        }

        if (action == "tree") {
            normalized["page_size"] = body.value("page_size", 100);
            auto parent = body.value("parent_device_id", std::string{});
            if (parent.empty())
                filter["node_kind"] = "station";
            else {
                if (!request.navigation_visible(parent))
                    throw ApiError(404, "NotFound", "device_not_found");
                filter["parent_device_id"] = parent;
            }
        } else if (filter.count("parent_device_id"))
            request.device(filter.at("parent_device_id"));

        std::string prefix;
        if (recursive && filter.count("parent_device_id")) {
            auto parent = request.device(filter.at("parent_device_id"));
            prefix = parent.at("path").get<std::string>();
            if (prefix != "/") prefix += "/";
            filter.erase("parent_device_id");
        }
        normalized["filters"] = filter;
        auto q =
            page_query(DeviceTable::Device, normalized,
                       {"parent_device_id", "node_kind", "device_type_code", "keyword", "enabled"},
                       action == "tree" ? 500 : 200);
        q.order_by = body.value("sort", std::string("sort_order"));
        if (!prefix.empty()) q.filters.push_back({"path", prefix, DeviceFilterOp::Prefix});
        const auto requested_offset = q.offset, requested_limit = q.limit;
        Json result = Json::array();
        std::int64_t total = 0;

        // 按数据库分页扫描授权集合，total 不泄漏不可见节点。
        q.offset = 0;
        q.limit = 200;
        for (;; q.offset += q.limit) {
            auto page = request.query(q);
            for (const auto& candidate : page.at("items")) {
                if (action == "tree") {
                    if (!request.navigation_visible(candidate.at("device_id"))) continue;
                } else {
                    try {
                        request.device(candidate.at("device_id"));
                    } catch (const ApiError& error) {
                        if (error.status == 404) continue;
                        throw;
                    }
                }
                Json item = Json::object();
                if (action == "tree") {
                    for (auto key : {"device_id", "parent_device_id", "node_kind", "code", "name"})
                        item[key] = candidate.at(key);
                    item["enabled"] = candidate.at("enabled").get<int>() != 0;
                } else
                    item = summary(request, candidate);
                if (!completeness.empty() && item.at("config_completeness") != completeness)
                    continue;
                if (static_cast<std::size_t>(total++) < requested_offset ||
                    result.size() >= requested_limit)
                    continue;
                if (action == "tree") {
                    DeviceQuery children;
                    children.filters = {{"parent_device_id", candidate.at("device_id")}};
                    children.limit = 100000;
                    std::size_t count = 0;
                    for (const auto& child : request.rows(children)) {
                        if (request.navigation_visible(child.at("device_id"))) ++count;
                    }
                    item["child_count"] = count;
                    item["has_children"] = count != 0;
                }
                result.push_back(std::move(item));
            }
            if (page.at("items").size() < q.limit) break;
        }
        return {{{"items", result},
                 {"total", total},
                 {"page", body.value("page", 1)},
                 {"page_size", requested_limit}}};
    }

    if (action == "detail") {
        fields(body, {"device_id"});
        auto row = request.device(text(body, "device_id"));
        auto detail = summary(request, row);
        Json ancestors = Json::array();
        auto parent = row.at("parent_device_id").get<std::string>();
        while (!parent.empty()) {
            auto node = request.one(DeviceTable::Device, "device_id", parent);
            try {
                request.device(node.at("device_id"));
                ancestors.push_back(public_row(node));
            } catch (const ApiError& error) {
                if (error.status != 404) throw;
                ancestors.push_back({{"device_id", node.at("device_id")},
                                     {"name", node.at("name")},
                                     {"node_kind", node.at("node_kind")},
                                     {"navigation_only", true}});
            }
            parent = node.at("parent_device_id");
        }
        detail["ancestors"] = ancestors;
        detail["source_device"] = row.at("source_device_id") == ""
                                      ? Json(nullptr)
                                      : public_row(request.device(row.at("source_device_id")));
        detail["capabilities"] = Json::array();
        return {detail};
    }

    if (action == "validate") {
        fields(body, {"device_id", "expected_config_revision", "changes"});
        auto id = text(body, "device_id");
        request.device(id);
        request.expect_revision(body);
        auto changes = body.value("changes", Json::array());
        if (!changes.empty()) {
            point_action(request, "/api/points/batch-change",
                         {{"device_id", id},
                          {"expected_config_revision", body.at("expected_config_revision")},
                          {"changes", changes}});
        }
        auto issues = request.validate(id);
        // 预检查可以写事务暂存候选，但必须回滚，不能让候选成为工程配置。
        take(request.transaction->rollback(request.context));
        request.transaction.reset();
        return {{{"valid", issues.empty()},
                 {"issues", issues},
                 {"counts", {{"errors", issues.size()}}},
                 {"affected_device_ids", Json::array({id})}}};
    }

    request.principal.require("device.write");
    fields(body, {"device_id", "expected_config_revision", "fields"});
    auto id = text(body, "device_id");
    if (action == "create") {
        auto input = body.at("fields");
        Json row = encode_record(CfgDeviceRow{});
        row["device_id"] = id;
        row["enabled"] = 0;
        row =
            merge_fields(row, input,
                         {"parent_device_id", "node_kind", "code", "name", "device_type_code",
                          "type_version", "source_device_id", "vendor", "model", "firmware_version",
                          "rated_power_kw", "template_id", "description", "sort_order", "enabled"});
        if (row.at("node_kind") == "unit") {
            for (auto key :
                 {"device_type_code", "type_version", "source_device_id", "template_id"}) {
                if (input.count(key))
                    throw ApiError(400, "InvalidArgument", "unit_field_not_allowed");
            }
        }
        auto parent = request.device(text(row, "parent_device_id"));
        text(row, "code");
        text(row, "name", 128);
        if (row.at("code").get<std::string>().find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos)
            throw ApiError(422, "InvalidArgument", "invalid_device_code");
        auto base = parent.at("path").get<std::string>();
        row["path"] = (base == "/" ? base : base + "/") + row.at("code").get<std::string>();
        row["depth"] = parent.at("depth").get<int>() + 1;
        if (row.at("node_kind") == "station")
            throw ApiError(422, "InvalidArgument", "station_creation_disabled");
        if (!row.at("source_device_id").get<std::string>().empty())
            request.device(row.at("source_device_id"));

        DeviceQuery existing;
        existing.filters = {{"device_id", id}};
        existing.limit = 1;
        auto old = request.query(existing).at("items");
        if (!old.empty()) {
            request.device(id);
            auto comparable = old.front();
            for (auto key : {"id", "created_at", "updated_at"}) {
                comparable.erase(key);
                row.erase(key);
            }
            if (comparable != row) throw ApiError(409, "Conflict", "stable_id_content_conflict");
            DeviceResult result{public_row(old.front())};
            result.meta["replayed"] = true;
            return result;
        }
        request.expect_revision(body);
        request.put(DeviceTable::Device, row);
        return {public_row(row), Json::object(), 201};
    }

    auto old = request.device(id);
    request.expect_revision(body);
    if (action == "update") {
        auto updated =
            old.at("node_kind") == "station"
                ? merge_fields(old, body.at("fields"),
                               {"name", "timezone", "default_stale_after_ms"})
                : merge_fields(old, body.at("fields"),
                               {"name", "vendor", "model", "firmware_version", "rated_power_kw",
                                "sort_order", "description", "enabled"});
        if (updated != old) request.put(DeviceTable::Device, updated);
        return {public_row(updated)};
    }
    if (action == "delete") {
        if (old.at("node_kind") == "station")
            throw ApiError(409, "Conflict", "station_delete_disabled");
        for (const auto& pair : {std::pair{DeviceTable::Device, "source_device_id"},
                                 {DeviceTable::ImportJob, "device_id"}}) {
            DeviceQuery q;
            q.table = pair.first;
            q.filters = {{pair.second, id}};
            q.limit = 1;
            if (!request.rows(q).empty()) throw ApiError(409, "Conflict", "device_is_referenced");
        }
        take(request.database.device_config->remove_device(*request.transaction, id,
                                                           request.context));
        request.changed = true;
        return {{{"removed_id", id}}};
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
