#include "device_service.h"
#include "web/platform/crypto.h"

namespace ems::web {
using namespace database;

DeviceResult catalog_action(DeviceRequest& request, const std::string& path, const Json& body) {
    request.principal.require("device.read");
    const auto action = path.substr(path.find_last_of('/') + 1);
    if (action == "capabilities") {
        fields(body, {});
        // 只声明已接通解析与持久化的交集，不按协议插件目录猜测能力。
        return {{{"protocols", Json::array({"modbus", "iec104", "dlt645"})},
                 {"import_formats", Json::array({"vendor_csv", "vendor_xlsx"})},
                 {"supported_point_types", Json::array({"yc", "yx", "yk", "yt"})},
                 {"supported_wire_layouts", Json::array({"ab", "ba"})},
                 {"supported_wire_types", Json::array({"bool", "i16", "u16"})},
                 {"shared_write_groups", false},
                 {"max_upload_bytes", 2 * 1024 * 1024},
                 {"max_rows", 10000}}};
    }
    const bool types = path.rfind("/api/device-types/", 0) == 0;
    const auto table = types ? DeviceTable::Type : DeviceTable::Template;
    if (action == "list") {
        fields(body, {"filters", "page", "page_size"});
        auto q =
            types ? page_query(table, body, {"category", "status"})
                  : page_query(table, body,
                               {"device_type_code", "type_version", "vendor", "model", "status"});
        auto result = request.query(q);
        for (auto& row : result["items"]) row = public_row(row);
        result["page"] = body.value("page", 1);
        result["page_size"] = q.limit;
        return {result};
    }
    if (types && action == "points") {
        fields(body, {"type_code", "type_version", "point_type", "page", "page_size"});
        auto q = page_query(DeviceTable::TypePoint, body, {});
        q.filters = {{"type_code", text(body, "type_code")},
                     {"type_version", text(body, "type_version")}};
        if (body.count("point_type")) {
            point_table(text(body, "point_type", 2));
            q.filters.push_back({"point_type", body.at("point_type")});
        }
        auto result = request.query(q);
        for (auto& row : result["items"]) row = public_row(row);
        result["page"] = body.value("page", 1);
        result["page_size"] = q.limit;
        return {result};
    }
    if (action == "detail" || action == "apply") {
        if (action == "detail")
            fields(body, {"template_id", "template_version"});
        else
            fields(body, {"device_id", "binding_id", "template_id", "template_version", "mode",
                          "expected_config_revision"});
        DeviceQuery q;
        q.table = DeviceTable::Template;
        q.filters = {{"template_id", text(body, "template_id")},
                     {"template_version", text(body, "template_version")}};
        q.limit = 1;
        auto templates = request.rows(q);
        if (templates.empty()) throw ApiError(404, "NotFound", "template_not_found");
        auto row = templates.front();
        if (action == "detail") return {public_row(row)};

        request.principal.require("device.write");
        request.principal.require("point.write");
        request.expect_revision(body);
        auto owner = request.device(text(body, "device_id"));
        if (owner.at("device_type_code") != row.at("device_type_code") ||
            owner.at("type_version") != row.at("type_version"))
            throw ApiError(422, "InvalidArgument", "template_type_mismatch");
        if (text(body, "mode") != "merge_missing")
            throw ApiError(422, "Unsupported", "template_mode_not_supported");
        auto binding = request.one(DeviceTable::Binding, "binding_id", text(body, "binding_id"));
        if (binding.at("device_id") != owner.at("device_id"))
            throw ApiError(422, "InvalidArgument", "binding_owner_mismatch");
        const auto points = Json::parse(row.at("points_json").get<std::string>());
        if (!points.is_object()) throw ApiError(500, "CorruptData", "invalid_template_points");
        Json created = Json::array(), skipped = Json::array();

        // 模板按四遥集合复制，存在的固有点或厂家键原样保留。
        for (auto kind : {"yc", "yx", "yk", "yt"}) {
            auto items = points.value(kind, Json::array());
            if (!items.is_array() || items.size() > 200)
                throw ApiError(422, "InvalidArgument", "template_point_limit");
            for (auto config : items) {
                auto semantic = config.value("semantic_code", std::string{});
                auto vendor = config.value("vendor_key", std::string{});
                DeviceQuery exists;
                exists.table = point_table(kind);
                exists.limit = 1;
                if (!semantic.empty())
                    exists.filters = {{"owner_device_id", owner.at("device_id")},
                                      {"semantic_code", semantic}};
                else if (!vendor.empty())
                    exists.filters = {{"binding_id", binding.at("binding_id")},
                                      {"vendor_key", vendor}};
                else
                    throw ApiError(422, "InvalidArgument", "template_point_identity_missing");
                auto old = request.rows(exists);
                if (!old.empty()) {
                    skipped.push_back(old.front().at("point_id"));
                    continue;
                }

                config["binding_id"] = binding.at("binding_id");
                config["origin"] = "template";
                auto id = "pt-" + random_token().substr(0, 40);
                point_action(request, "/api/points/create",
                             {{"device_id", owner.at("device_id")},
                              {"point_type", kind},
                              {"point_id", id},
                              {"expected_config_revision", body.at("expected_config_revision")},
                              {"fields", config}});
                created.push_back(id);
            }
        }
        return {{{"created_point_ids", created},
                 {"skipped_point_ids", skipped},
                 {"warnings", Json::array()}}};
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
