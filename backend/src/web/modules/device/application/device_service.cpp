#include "device_service.h"

#include <common/database/configuration/device_management_validation.h>

#include <algorithm>
#include <limits>
#include <set>

#include "web/modules/device/infrastructure/point_table_parser.h"
#include "web/platform/crypto.h"

namespace ems::web {
using namespace database;

namespace {
std::string sql_time() {
    auto value = utc_now();
    value[10] = ' ';
    value.pop_back();
    return value;
}
}  // namespace

DeviceRequest::DeviceRequest(MySqlDatabaseContext& db, const Principal& actor,
                             core::CallContext ctx, bool write)
    : database(db), principal(actor), context(std::move(ctx)) {
    transaction = take(database.database->begin(context));

    // 先锁站根再读业务数据；读请求用共享锁，写请求独占锁。
    DeviceQuery q;
    q.table = DeviceTable::Device;
    q.filters = {{"node_kind", "station"}};
    q.limit = 2;
    q.for_update = write;
    q.for_share = !write;
    auto roots = query(q).at("items");
    if (roots.size() != 1) throw ApiError(503, "NotReady", "unique_station_required");

    station = roots.front();
    auto id = station.at("device_id").get<std::string>();
    if (!principal.station_ids.count("*") && !principal.station_ids.count(id)) {
        throw ApiError(403, "PermissionDenied", "station_not_allowed");
    }
}

Json DeviceRequest::query(DeviceQuery q) {
    auto page = take(database.device_management->query(*transaction, q, context));
    Json rows = Json::array();
    for (const auto& row : page.rows) rows.push_back(encode_record(row));
    return {{"items", std::move(rows)}, {"total", page.total}};
}

Json DeviceRequest::rows(DeviceQuery q) { return query(std::move(q)).at("items"); }

Json DeviceRequest::one(DeviceTable table, const std::string& field, const std::string& value,
                        bool lock) {
    DeviceQuery q;
    q.table = table;
    q.filters = {{field, value}};
    q.limit = 2;
    q.for_update = lock;
    auto rows = query(q).at("items");
    if (rows.size() != 1) throw ApiError(404, "NotFound", "object_not_found");
    return rows.front();
}

Json DeviceRequest::device(const std::string& id) {
    auto target = one(DeviceTable::Device, "device_id", id);
    auto cursor = target;
    std::set<std::string> seen;
    bool allowed = principal.device_roots.count("*") != 0;

    // 每个归属 ID 都按实际父链验证，不能信任请求中的路径或字符串前缀。
    for (int depth = 0; depth <= 8; ++depth) {
        auto node = cursor.at("device_id").get<std::string>();
        if (!seen.insert(node).second) throw ApiError(500, "CorruptData", "device_cycle");
        allowed = allowed || principal.device_roots.count(node) != 0;
        if (cursor.at("node_kind") == "station") {
            if (node != station.at("device_id") || !allowed)
                throw ApiError(404, "NotFound", "device_not_found");
            return target;
        }
        cursor = one(DeviceTable::Device, "device_id", text(cursor, "parent_device_id"));
    }
    throw ApiError(500, "CorruptData", "device_depth");
}

bool DeviceRequest::navigation_visible(const std::string& id) {
    try {
        device(id);
        return true;
    } catch (const ApiError& error) {
        if (error.status != 404) throw;
    }

    // 祖先只用于导航，不因其可见而授予档案读取或修改权限。
    for (const auto& root : principal.device_roots) {
        std::string current = root;
        for (int depth = 0; depth <= 8 && !current.empty(); ++depth) {
            Json row;
            try {
                row = one(DeviceTable::Device, "device_id", current);
            } catch (const ApiError& error) {
                if (error.status == 404) break;
                throw;
            }
            if (current == id) return true;
            current = row.at("parent_device_id").get<std::string>();
        }
    }
    return false;
}

void DeviceRequest::expect_revision(const Json& body) {
    auto expected = text(body, "expected_config_revision", 20);
    auto current = std::to_string(station.at("config_revision").get<std::int64_t>());
    if (expected != current) {
        throw ApiError(
            409, "Conflict", "config_revision_mismatch", "NotApplied",
            Json::array({{{"field", "expected_config_revision"}, {"current", current}}}));
    }
}

void DeviceRequest::put(DeviceTable table, Json row, bool engineering) {
    // 补齐默认值再写入；内部 ID 不来自 HTTP，日期统一按 UTC 存库。
    row = encode_record(decode_record(table, row));
    if (row.count("created_at") && row.at("created_at") == "") row["created_at"] = sql_time();
    if (row.count("updated_at")) row["updated_at"] = sql_time();
    take(database.device_management->put(*transaction, decode_record(table, row), context));
    changed = changed || engineering;
}

DeviceResult DeviceRequest::finish(DeviceResult result) {
    if (changed) {
        auto revision = station.at("config_revision").get<std::int64_t>();
        if (revision == std::numeric_limits<std::int64_t>::max())
            throw ApiError(409, "Conflict", "revision_exhausted");
        station["config_revision"] = revision + 1;
        put(DeviceTable::Device, station, false);
    }

    // 只有 commit 成功才向客户端报告保存完成；公共层的 Unknown 不被覆盖。
    take(transaction->commit(context));
    result.meta["config_revision"] =
        std::to_string(station.at("config_revision").get<std::int64_t>());
    return result;
}

Json DeviceRequest::validate(const std::string& device_id) {
    auto records = take(database.device_config->load_configuration(*transaction, context));
    Json issues = Json::array();
    std::vector<std::string> confirmations;
    auto confirmed = [&](const auto& points) {
        for (const auto& point : points) {
            const auto separator = point.source_import_row_id.find_last_of(':');
            if (separator == std::string::npos) continue;
            auto job = point.source_import_row_id.substr(0, separator);
            auto line = point.source_import_row_id.substr(separator + 1);
            DeviceQuery q;
            q.table = DeviceTable::ImportRow;
            q.filters = {{"import_job_id", job}, {"line_no", line}};
            q.limit = 1;
            auto entries = rows(q);
            if (entries.empty()) continue;
            const auto& row = entries.front();
            if (row.at("confirm_state") != "accepted" && row.at("confirm_state") != "fixed")
                continue;
            if (row.at("parse_code") != "address_overlap") continue;
            auto owner = std::find_if(
                records.devices.begin(), records.devices.end(),
                [&](const auto& value) { return value.device_id == point.owner_device_id; });
            if (owner == records.devices.end() || row.at("target_device_path") != owner->path)
                continue;
            DeviceQuery jobs;
            jobs.table = DeviceTable::ImportJob;
            jobs.filters = {{"import_job_id", job}};
            jobs.limit = 1;
            auto task = rows(jobs);
            if (task.empty() || task.front().at("binding_id") != point.binding_id) continue;
            if (row.at("address_space") == point.address_space &&
                row.at("address_offset") == point.address_offset &&
                row.at("register_count") == point.register_count &&
                row.at("bit_offset") == point.bit_offset)
                confirmations.push_back(point.point_id);
        }
    };
    confirmed(records.points_yc);
    confirmed(records.points_yx);
    confirmed(records.points_yk);
    confirmed(records.points_yt);
    for (const auto& finding : database::validate_device_management(records, confirmations)) {
        if (!device_id.empty() && !finding.device_id.empty() && finding.device_id != device_id)
            continue;
        auto visible_device = finding.device_id;
        auto visible_point = finding.point_id;
        if (!visible_device.empty()) {
            try {
                device(visible_device);
            } catch (const ApiError& error) {
                if (error.status != 404) throw;
                // 全局约束仍然阻止提交，但错误详情不能泄露授权范围之外的 ID。
                visible_device.clear();
                visible_point.clear();
            }
        }
        issues.push_back({{"severity", "error"},
                          {"reason", finding.reason},
                          {"device_id", visible_device},
                          {"point_id", visible_point}});
    }
    return issues;
}

DeviceService::DeviceService(MySqlDatabaseContext db, std::string workspace)
    : database_(std::move(db)), workspace_(std::move(workspace)) {}

DeviceResult DeviceService::execute(const std::string& path, const Json& body,
                                    const Principal& actor, const core::CallContext& ctx,
                                    const Upload& upload) {
    Upload prepared = upload;
    if (path == "/api/point-imports/create") {
        // 预授权用短事务完成；真正的文件解析绝不占用连接或站根锁。
        {
            DeviceRequest preflight(database_, actor, ctx, false);
            actor.require("import.write");
            auto owner = preflight.device(text(body, "device_id"));
            auto binding =
                preflight.one(DeviceTable::Binding, "binding_id", text(body, "binding_id"));
            if (owner.at("node_kind") != "device" ||
                binding.at("device_id") != owner.at("device_id"))
                throw ApiError(422, "InvalidArgument", "binding_owner_mismatch");
            preflight.finish({Json::object()});
        }
        prepared.parsed = parse_upload(body, upload, workspace_);
    }
    const auto action = path.substr(path.find_last_of('/') + 1);
    const bool write = action == "create" || action == "update" || action == "delete" ||
                       action == "batch-change" || action == "apply" || action == "validate";
    const bool preview = path.rfind("/api/point-imports/", 0) == 0 && action != "apply";
    DeviceRequest request(database_, actor, ctx,
                          (write && !preview) || path == "/api/point-imports/create");
    DeviceResult result;

    // 固定模块分派，不从 URL 或 JSON 拼接类名、函数名或表名。
    if (path.rfind("/api/devices/", 0) == 0 || path.rfind("/api/device-management/", 0) == 0) {
        if (action == "capabilities")
            result = catalog_action(request, path, body);
        else
            result = device_action(request, path, body);
    } else if (path.rfind("/api/channels/", 0) == 0 || path.rfind("/api/bindings/", 0) == 0) {
        result = binding_action(request, path, body);
    } else if (path.rfind("/api/points/", 0) == 0 || path.rfind("/api/inherent-points/", 0) == 0) {
        result = point_action(request, path, body);
    } else if (path.rfind("/api/point-imports/", 0) == 0) {
        result = import_action(request, path, body, prepared, workspace_);
    } else {
        result = catalog_action(request, path, body);
    }

    if (!request.transaction) {
        result.meta["config_revision"] =
            std::to_string(request.station.at("config_revision").get<std::int64_t>());
        return result;
    }
    if (request.changed) {
        auto issues = request.validate();
        if (!issues.empty())
            throw ApiError(422, "InvalidArgument", "configuration_invalid", "NotApplied", issues);
    }
    return request.finish(std::move(result));
}

void DeviceService::check_schema(const core::CallContext& context) {
    auto tx = take(database_.database->begin(context));
    for (int i = 0; i < 14; ++i) {
        DeviceQuery q;
        q.table = static_cast<DeviceTable>(i);
        q.limit = 1;
        take(database_.device_management->query(*tx, q, context));
    }
    DeviceQuery roots;
    roots.filters = {{"node_kind", "station"}};
    roots.limit = 2;
    if (take(database_.device_management->query(*tx, roots, context)).rows.size() != 1)
        throw ApiError(503, "NotReady", "unique_station_required");
    take(tx->rollback(context));
}

bool same_record(Json existing, Json candidate) {
    for (auto key : {"id", "created_at", "updated_at"}) {
        existing.erase(key);
        candidate.erase(key);
    }
    return existing == candidate;
}

Json public_row(Json row) {
    row.erase("id");
    row.erase("legacy_ref");

    // 数据库 JSON 文本在边界转换为对象，64 位数字在浏览器侧保持精度。
    for (const auto& pair : {std::pair{"serial_json", "serial"},
                             {"enum_json", "enum_map"},
                             {"protocol_address_json", "protocol_address"},
                             {"feedback_json", "feedback"},
                             {"capability_codes_json", "capability_codes"},
                             {"points_json", "points"}}) {
        if (!row.count(pair.first)) continue;
        auto value = row.at(pair.first).get<std::string>();
        row[pair.second] = value.empty() ? Json(nullptr) : Json::parse(value);
        row.erase(pair.first);
    }
    for (auto key : {"config_revision", "fixed_raw_value", "request_total", "success_total",
                     "timeout_total", "error_total"}) {
        if (row.count(key) && row.at(key).is_number_integer())
            row[key] = std::to_string(row.at(key).get<std::int64_t>());
    }
    for (auto key : {"enabled", "can_collect", "can_control", "can_forward", "required", "writable",
                     "soe", "invert"}) {
        if (row.count(key)) row[key] = row.at(key).get<int>() != 0;
    }
    for (auto key : {"created_at", "updated_at", "submitted_at", "finished_at", "confirm_at"}) {
        if (!row.count(key)) continue;
        auto value = row.at(key).get<std::string>();
        if (value.empty())
            row[key] = nullptr;
        else {
            if (value.size() > 10) value[10] = 'T';
            row[key] = value + "Z";
        }
    }
    if (row.count("address_offset")) {
        row["address"] = {
            {"space", row.at("address_space") == "" ? Json(nullptr) : row.at("address_space")},
            {"offset", row.at("address_offset")},
            {"bit_offset", row.at("bit_offset")},
            {"register_count", row.at("register_count")}};
        if (row.count("protocol_address")) {
            row["address"]["protocol_address"] = row.at("protocol_address");
            row.erase("protocol_address");
        }
        for (auto key : {"address_space", "address_offset", "bit_offset", "register_count"})
            row.erase(key);
    }
    return row;
}

DeviceQuery page_query(DeviceTable table, const Json& body,
                       std::initializer_list<const char*> allowed, std::size_t maximum) {
    DeviceQuery q;
    q.table = table;
    const int page = body.value("page", 1);
    const int size = body.value("page_size", 20);
    if (page < 1 || size < 1 || static_cast<std::size_t>(size) > maximum || page > 500000)
        throw ApiError(400, "InvalidArgument", "pagination_bounds");
    q.limit = static_cast<std::size_t>(size);
    q.offset = static_cast<std::size_t>(page - 1) * size;

    auto filter = body.value("filters", Json::object());
    fields(filter, allowed);
    for (auto it = filter.begin(); it != filter.end(); ++it) {
        if (it.key() == "keyword") {
            auto value = it.value().get<std::string>();
            std::string escaped;
            for (char c : value) {
                if (c == '!' || c == '%' || c == '_') escaped += '!';
                escaped += c;
            }
            q.filters.push_back({"name", "%" + escaped + "%", DeviceFilterOp::Like});
        } else if (it.value().is_boolean())
            q.filters.push_back({it.key(), it.value().get<bool>() ? "1" : "0"});
        else
            q.filters.push_back({it.key(), it.value().get<std::string>()});
    }
    if (body.count("sort")) {
        q.order_by = text(body, "sort", 32);
        if (q.order_by != "name" && q.order_by != "code" && q.order_by != "sort_order" &&
            q.order_by != "updated_at")
            throw ApiError(400, "InvalidArgument", "sort_not_allowed");
    }
    const auto order = body.value("order", std::string("asc"));
    if (order != "asc" && order != "desc")
        throw ApiError(400, "InvalidArgument", "sort_not_allowed");
    q.descending = order == "desc";
    return q;
}

Json merge_fields(Json current, const Json& patch, std::initializer_list<const char*> allowed) {
    fields(patch, allowed);
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        auto key = it.key();
        auto value = it.value();
        if (key == "address") {
            fields(value, {"space", "offset", "bit_offset", "register_count", "protocol_address"});
            if (value.count("space")) current["address_space"] = value.at("space");
            for (auto pair : {std::pair{"offset", "address_offset"},
                              {"bit_offset", "bit_offset"},
                              {"register_count", "register_count"}}) {
                if (value.count(pair.first)) current[pair.second] = value.at(pair.first);
            }
            if (value.count("protocol_address"))
                current["protocol_address_json"] = value.at("protocol_address").dump();
            continue;
        }
        for (auto pair : {std::pair{"serial", "serial_json"},
                          {"enum_map", "enum_json"},
                          {"feedback", "feedback_json"}}) {
            if (key == pair.first) {
                key = pair.second;
                value = value.dump();
            }
        }
        if (current.count(key) && current.at(key).is_string() && !value.is_string()) {
            if (key == "byte_order" && value.is_null())
                value = "";
            else
                throw ApiError(400, "InvalidArgument", "field_type_mismatch");
        }
        if (key == "fixed_raw_value" && value.is_string()) {
            auto number = value.get<std::string>();
            std::size_t used = 0;
            const auto parsed = std::stoll(number, &used);
            if (used != number.size()) throw ApiError(400, "InvalidArgument", "invalid_int64");
            value = parsed;
        }
        if (current.count(key) && current.at(key).is_number()) {
            const bool flag = key == "enabled" || key == "soe" || key == "invert" ||
                              key == "can_collect" || key == "can_control" || key == "can_forward";
            if (flag) {
                if (!value.is_boolean()) throw ApiError(400, "InvalidArgument", "boolean_required");
                value = value.get<bool>() ? 1 : 0;
            } else if (!value.is_number() ||
                       (current.at(key).is_number_integer() && !value.is_number_integer()))
                throw ApiError(400, "InvalidArgument", "number_required");
        }
        current[key] = std::move(value);
    }
    return current;
}

DeviceTable point_table(const std::string& kind) {
    if (kind == "yc") return DeviceTable::Yc;
    if (kind == "yx") return DeviceTable::Yx;
    if (kind == "yk") return DeviceTable::Yk;
    if (kind == "yt") return DeviceTable::Yt;
    throw ApiError(422, "InvalidArgument", "point_type_mismatch");
}
}  // namespace ems::web
