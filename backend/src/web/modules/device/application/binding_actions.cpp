#include "device_service.h"

namespace ems::web {
using namespace database;

namespace {
Json channel(DeviceRequest& request, const std::string& id) {
    auto row = request.one(DeviceTable::Channel, "channel_id", id);
    DeviceQuery q;
    q.table = DeviceTable::Binding;
    q.filters = {{"channel_id", id}};
    q.limit = 100000;
    auto bindings = request.rows(q);
    if (bindings.empty() && !request.principal.device_roots.count("*"))
        throw ApiError(404, "NotFound", "channel_not_found");

    // 共享连接的修改可能影响多个设备，必须对所有绑定检查范围。
    for (const auto& binding : bindings) request.device(binding.at("device_id"));
    return row;
}

Json new_channel(const std::string& id, const Json& patch) {
    Json row = encode_record(CfgChannelRow{});
    row["channel_id"] = id;
    row["enabled"] = 0;
    row["serial_json"] = "{}";
    row = merge_fields(
        row, patch,
        {"code", "name", "protocol", "conn_mode", "endpoint", "serial", "poll_interval_ms",
         "timeout_ms", "retry_times", "max_sessions", "enabled", "description"});
    for (auto key : {"code", "name", "protocol", "conn_mode", "endpoint"})
        text(row, key, key == std::string("endpoint") ? 255 : 128);
    if (row.at("protocol") != "modbus" && row.at("protocol") != "iec104" &&
        row.at("protocol") != "dlt645")
        throw ApiError(422, "Unsupported", "protocol_not_supported");
    return row;
}
}  // namespace

DeviceResult binding_action(DeviceRequest& request, const std::string& path, const Json& body) {
    const bool is_channel = path.rfind("/api/channels/", 0) == 0;
    const auto action = path.substr(path.find_last_of('/') + 1);
    request.principal.require(is_channel ? "channel.read" : "device.read");

    if (is_channel) {
        if (action == "list") {
            fields(body, {"filters", "page", "page_size"});
            auto q = page_query(DeviceTable::Channel, body, {"protocol", "enabled", "keyword"});
            const auto offset = q.offset, limit = q.limit;
            Json items = Json::array();
            std::size_t total = 0;
            q.offset = 0;
            q.limit = 200;
            for (;; q.offset += q.limit) {
                auto rows = request.rows(q);
                for (const auto& row : rows) {
                    try {
                        channel(request, row.at("channel_id"));
                    } catch (const ApiError& error) {
                        if (error.status == 404) continue;
                        throw;
                    }
                    if (total++ >= offset && items.size() < limit) items.push_back(public_row(row));
                }
                if (rows.size() < q.limit) break;
            }
            return {{{"items", items},
                     {"total", total},
                     {"page", body.value("page", 1)},
                     {"page_size", limit}}};
        }
        if (action == "detail") {
            fields(body, {"channel_id"});
            return {public_row(channel(request, text(body, "channel_id")))};
        }

        request.principal.require("channel.write");
        fields(body, {"channel_id", "expected_config_revision", "fields"});
        auto id = text(body, "channel_id");
        if (action == "create") {
            // 限定设备子树的账号不能创建无法归属的独立通道。
            if (!request.principal.device_roots.count("*"))
                throw ApiError(403, "PermissionDenied", "unscoped_channel_creation");
            auto row = new_channel(id, body.at("fields"));
            DeviceQuery existing;
            existing.table = DeviceTable::Channel;
            existing.filters = {{"channel_id", id}};
            existing.limit = 1;
            auto saved = request.rows(existing);
            if (!saved.empty()) {
                channel(request, id);
                if (!same_record(saved.front(), row))
                    throw ApiError(409, "Conflict", "channel_id_conflict");
                DeviceResult replay{public_row(saved.front())};
                replay.meta["replayed"] = true;
                return replay;
            }
            request.expect_revision(body);
            request.put(DeviceTable::Channel, row);
            return {public_row(row), Json::object(), 201};
        }
        request.expect_revision(body);
        auto old = channel(request, id);
        if (action == "update") {
            auto row = merge_fields(old, body.at("fields"),
                                    {"name", "endpoint", "serial", "poll_interval_ms", "timeout_ms",
                                     "retry_times", "max_sessions", "enabled", "description"});
            if (row != old) request.put(DeviceTable::Channel, row);
            return {public_row(row)};
        }
        if (action == "delete") {
            take(request.database.device_config->remove_channel(*request.transaction, id,
                                                                request.context));
            request.changed = true;
            return {{{"removed_id", id}}};
        }
    } else {
        auto device_id = text(body, "device_id");
        auto owner = request.device(device_id);
        if (owner.at("node_kind") != "device")
            throw ApiError(422, "InvalidArgument", "component_cannot_bind");
        if (action == "list") {
            fields(body, {"device_id", "page", "page_size"});
            auto q = page_query(DeviceTable::Binding, body, {});
            q.filters = {{"device_id", device_id}};
            auto result = request.query(q);
            for (auto& row : result["items"]) {
                auto connection =
                    request.one(DeviceTable::Channel, "channel_id", row.at("channel_id"));
                row = public_row(row);
                row["channel"] = public_row(connection);
            }
            result["page"] = body.value("page", 1);
            result["page_size"] = q.limit;
            return {result};
        }

        request.principal.require("binding.write");
        fields(body, {"device_id", "binding_id", "expected_config_revision", "fields"});
        auto id = text(body, "binding_id");
        if (action == "create") {
            auto patch = body.at("fields");
            fields(patch, {"channel_id", "new_channel", "device_address", "profile_version",
                           "can_collect", "can_control", "can_forward", "enabled", "description"});
            if (patch.count("channel_id") == patch.count("new_channel"))
                throw ApiError(400, "InvalidArgument", "select_one_channel_source");
            DeviceQuery replay_query;
            replay_query.table = DeviceTable::Binding;
            replay_query.filters = {{"binding_id", id}};
            replay_query.limit = 1;
            auto saved_binding = request.rows(replay_query);
            if (!saved_binding.empty()) {
                if (saved_binding.front().at("device_id") != device_id)
                    throw ApiError(404, "NotFound", "binding_not_found");
                auto replay_patch = patch;
                if (replay_patch.count("new_channel")) {
                    request.principal.require("channel.write");
                    auto input = replay_patch.at("new_channel");
                    auto connection_id = text(input, "channel_id");
                    input.erase("channel_id");
                    auto saved_channel = channel(request, connection_id);
                    if (!same_record(saved_channel, new_channel(connection_id, input)))
                        throw ApiError(409, "Conflict", "stable_id_content_conflict");
                    replay_patch.erase("new_channel");
                    replay_patch["channel_id"] = connection_id;
                } else {
                    request.principal.require("channel.read");
                    channel(request, text(replay_patch, "channel_id"));
                }
                Json candidate = encode_record(CfgBindingRow{});
                candidate["binding_id"] = id;
                candidate["device_id"] = device_id;
                candidate["enabled"] = 0;
                candidate =
                    merge_fields(candidate, replay_patch,
                                 {"channel_id", "device_address", "profile_version", "can_collect",
                                  "can_control", "can_forward", "enabled", "description"});
                if (!same_record(saved_binding.front(), candidate))
                    throw ApiError(409, "Conflict", "stable_id_content_conflict");
                DeviceResult replay{public_row(saved_binding.front())};
                replay.meta["replayed"] = true;
                return replay;
            }
            request.expect_revision(body);
            std::string channel_id;
            if (patch.count("new_channel")) {
                request.principal.require("channel.write");
                auto input = patch.at("new_channel");
                channel_id = text(input, "channel_id");
                input.erase("channel_id");
                auto connection = new_channel(channel_id, input);
                DeviceQuery existing;
                existing.table = DeviceTable::Channel;
                existing.filters = {{"channel_id", channel_id}};
                existing.limit = 1;
                if (!request.rows(existing).empty())
                    throw ApiError(409, "Conflict", "channel_id_conflict");
                request.put(DeviceTable::Channel, connection);
                patch.erase("new_channel");
                patch["channel_id"] = channel_id;
            } else {
                channel_id = text(patch, "channel_id");
                request.principal.require("channel.read");
                channel(request, channel_id);
            }
            DeviceQuery duplicate;
            duplicate.table = DeviceTable::Binding;
            duplicate.filters = {{"device_id", device_id}, {"channel_id", channel_id}};
            duplicate.limit = 1;
            auto old = request.rows(duplicate);
            if (!old.empty())
                throw ApiError(
                    409, "Conflict", "binding_pair_conflict", "NotApplied",
                    Json::array({{{"existing_binding_id", old.front().at("binding_id")}}}));
            duplicate.filters = {{"binding_id", id}};
            if (!request.rows(duplicate).empty())
                throw ApiError(409, "Conflict", "binding_id_conflict");

            Json row = encode_record(CfgBindingRow{});
            row["binding_id"] = id;
            row["device_id"] = device_id;
            row["enabled"] = 0;
            row = merge_fields(row, patch,
                               {"channel_id", "device_address", "profile_version", "can_collect",
                                "can_control", "can_forward", "enabled", "description"});
            text(row, "device_address");
            request.put(DeviceTable::Binding, row);
            return {public_row(row), Json::object(), 201};
        }
        request.expect_revision(body);
        auto old = request.one(DeviceTable::Binding, "binding_id", id);
        if (old.at("device_id") != device_id) throw ApiError(404, "NotFound", "binding_not_found");
        if (action == "update") {
            auto row = merge_fields(old, body.at("fields"),
                                    {"device_address", "profile_version", "can_collect",
                                     "can_control", "can_forward", "enabled", "description"});
            if (row != old) request.put(DeviceTable::Binding, row);
            return {public_row(row)};
        }
        if (action == "delete") {
            DeviceQuery jobs;
            jobs.table = DeviceTable::ImportJob;
            jobs.filters = {{"binding_id", id}};
            jobs.limit = 1;
            if (!request.rows(jobs).empty())
                throw ApiError(409, "Conflict", "binding_referenced_by_import");
            take(request.database.device_config->remove_binding(*request.transaction, id,
                                                                request.context));
            request.changed = true;
            return {{{"removed_id", id}}};
        }
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
