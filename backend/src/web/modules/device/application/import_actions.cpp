#include <algorithm>
#include <set>

#include "device_service.h"
#include "web/platform/crypto.h"

namespace ems::web {
using namespace database;

namespace {
Json import_rows(DeviceRequest& request, const std::string& job) {
    DeviceQuery q;
    q.table = DeviceTable::ImportRow;
    q.filters = {{"import_job_id", job}};
    q.limit = 100000;
    q.order_by = "line_no";
    return request.rows(q);
}

std::string preview_hash(const Json& job, Json rows) {
    for (auto& row : rows) {
        row.erase("id");
        row.erase("confirm_at");
    }
    return sha256(Json{
        {"device_id", job.at("device_id")},
        {"binding_id", job.at("binding_id")},
        {"source_kind", job.at("source_kind")},
        {"address_base", job.at("address_base")},
        {"rows",
         rows}}.dump());
}

void expect_preview(const Json& body, const Json& job, const Json& rows) {
    if (text(body, "expected_preview_hash", 64) != preview_hash(job, rows))
        throw ApiError(409, "Conflict", "preview_changed");
}

Json checked_rows(DeviceRequest& request, Json& job, Json rows) {
    job["row_total"] = rows.size();
    job["row_ok"] = 0;
    job["row_warning"] = 0;
    job["row_error"] = 0;
    bool complete = true;
    auto binding = request.one(DeviceTable::Binding, "binding_id", job.at("binding_id"));
    Json occupancy = Json::array();
    DeviceQuery shared;
    shared.table = DeviceTable::Binding;
    shared.limit = 100000;
    shared.filters = {{"channel_id", binding.at("channel_id")},
                      {"device_address", binding.at("device_address")}};
    for (const auto& route : request.rows(shared)) {
        for (auto kind : {"yc", "yx", "yk", "yt"}) {
            DeviceQuery points;
            points.table = point_table(kind);
            points.limit = 100000;
            points.filters = {{"binding_id", route.at("binding_id")}};
            for (auto point : request.rows(points)) {
                if (point.at("enabled") == 0) continue;
                point["point_type"] = kind;
                occupancy.push_back(point);
            }
        }
    }

    // 重新解析每条归属及分类，确认操作不能把硬错误变为可接受 warning。
    for (auto& row : rows) {
        row["parse_status"] = "ok";
        row["parse_code"] = "";
        row["parse_message"] = "";
        if (row.at("confirm_state") == "rejected") continue;
        try {
            auto target =
                request.one(DeviceTable::Device, "path", text(row, "target_device_path", 512));
            request.device(target.at("device_id"));
            auto source = target.at("node_kind") == "component" ? target.at("source_device_id")
                                                                : target.at("device_id");
            if (source != job.at("device_id"))
                throw ApiError(422, "InvalidArgument", "invalid_source_device");
            const auto kind = text(row, "point_type", 2);
            point_table(kind);
            auto offset = row.at("address_offset").get<int>();
            auto count = row.at("register_count").get<int>();
            if (offset < 0 || count < 1 || count > 4 || offset + count > 65536 ||
                row.at("scale_factor").get<double>() == 0)
                throw ApiError(422, "InvalidArgument", "import_value_bounds");
            if (row.at("byte_order") != "" && row.at("byte_order") != "ab" &&
                row.at("byte_order") != "ba")
                throw ApiError(422, "Unsupported", "wire_layout_unsupported");
            if (!row.at("semantic_code").get<std::string>().empty()) {
                DeviceQuery q;
                q.table = DeviceTable::TypePoint;
                q.limit = 1;
                q.filters = {{"type_code", target.at("device_type_code")},
                             {"type_version", target.at("type_version")},
                             {"semantic_code", row.at("semantic_code")},
                             {"point_type", kind}};
                if (request.rows(q).empty())
                    throw ApiError(422, "InvalidArgument", "point_type_mismatch");
            }
            if (row.at("vendor_key") == "")
                throw ApiError(422, "InvalidArgument", "vendor_key_required");
            auto overlap = [&](const Json& other, bool same_import) {
                if (other.at("address_space") != row.at("address_space")) return;
                if (other.value("vendor_key", std::string{}) == row.at("vendor_key") &&
                    (same_import ||
                     other.value("binding_id", std::string{}) == job.at("binding_id")))
                    return;
                const int begin = row.at("address_offset").get<int>(), end = begin + count;
                const int other_begin = other.at("address_offset").get<int>();
                const int other_end = other_begin + other.at("register_count").get<int>();
                if (begin >= other_end || other_begin >= end) return;
                const auto other_kind = other.at("point_type").get<std::string>();
                const bool write = kind == "yk" || kind == "yt";
                const bool other_write = other_kind == "yk" || other_kind == "yt";
                if (write && other_write)
                    throw ApiError(422, "InvalidArgument", "address_write_conflict");
                if (write != other_write) {
                    row["parse_status"] = "warning";
                    row["parse_code"] = "address_overlap";
                    row["parse_message"] = "需确认读写共享地址语义";
                }
            };
            for (const auto& other : occupancy) overlap(other, false);
            for (const auto& other : rows)
                if (other.at("line_no") != row.at("line_no") &&
                    other.at("confirm_state") != "rejected")
                    overlap(other, true);
        } catch (const ApiError& error) {
            row["parse_status"] = "error";
            row["parse_code"] = error.reason;
            row["parse_message"] = error.reason;
        }
        const auto status = row.at("parse_status").get<std::string>();
        const auto counter =
            status == "error" ? "row_error" : (status == "warning" ? "row_warning" : "row_ok");
        job[counter] = job.at(counter).get<int>() + 1;
        if (status == "error" || (status == "warning" && row.at("confirm_state") != "accepted" &&
                                  row.at("confirm_state") != "fixed"))
            complete = false;
    }
    if (job.at("status") != "applied" && job.at("status") != "failed")
        job["status"] = complete ? "confirmed" : "parsed";
    return rows;
}

void save_preview(DeviceRequest& request, Json job, const Json& rows) {
    request.put(DeviceTable::ImportJob, job, false);
    for (const auto& row : rows) request.put(DeviceTable::ImportRow, row, false);
}

Json detail(Json job, const Json& rows) {
    const auto hash = preview_hash(job, rows);
    job = public_row(job);
    job["preview_hash"] = hash;
    job["allowed_operations"] = job.at("status") == "applied" || job.at("status") == "failed"
                                    ? Json::array()
                                    : Json::array({"edit_rows", "validate", "apply"});
    return job;
}
}  // namespace

DeviceResult import_action(DeviceRequest& request, const std::string& path, const Json& body,
                           const Upload& upload, const std::string&) {
    request.principal.require("device.read");
    const auto action = path.substr(path.find_last_of('/') + 1);
    if (action == "list") {
        fields(body, {"device_id", "page", "page_size"});
        auto id = text(body, "device_id");
        request.device(id);
        auto q = page_query(DeviceTable::ImportJob, body, {});
        q.filters = {{"device_id", id}};
        auto result = request.query(q);
        for (auto& row : result["items"]) row = public_row(row);
        result["page"] = body.value("page", 1);
        result["page_size"] = q.limit;
        return {result};
    }
    if (action == "create") {
        request.principal.require("import.write");
        fields(body, {"device_id", "binding_id", "source_kind", "address_base"});
        auto owner = request.device(text(body, "device_id"));
        auto binding = request.one(DeviceTable::Binding, "binding_id", text(body, "binding_id"));
        if (owner.at("node_kind") != "device" || binding.at("device_id") != owner.at("device_id"))
            throw ApiError(422, "InvalidArgument", "binding_owner_mismatch");
        if (upload.parsed.is_null()) throw ApiError(500, "Internal", "import_not_preparsed");
        auto digest = sha256(upload.bytes);
        auto key = sha256(body.at("device_id").get<std::string>() + "|" +
                          body.at("binding_id").get<std::string>() + "|" + digest);
        DeviceQuery lookup;
        lookup.table = DeviceTable::ImportJob;
        lookup.filters = {{"idempotency_key", key}};
        lookup.limit = 1;
        auto existing = request.rows(lookup);
        if (!existing.empty()) {
            auto old = existing.front();
            if (old.at("source_kind") != body.at("source_kind") ||
                old.at("address_base") != body.at("address_base"))
                throw ApiError(409, "Conflict", "idempotency_options_mismatch");
            DeviceResult result{detail(old, import_rows(request, old.at("import_job_id")))};
            result.meta["replayed"] = true;
            return result;
        }
        Json job = encode_record(CfgImportJobRow{});
        auto id = "imp-" + random_token().substr(0, 40);
        job["import_job_id"] = id;
        job["device_id"] = owner.at("device_id");
        job["binding_id"] = binding.at("binding_id");
        job["source_kind"] = body.at("source_kind");
        job["address_base"] = body.at("address_base");
        job["file_name"] = upload.filename;
        job["file_sha256"] = digest;
        job["file_bytes"] = upload.bytes.size();
        job["idempotency_key"] = key;
        job["submitted_by"] = request.principal.subject_id;
        auto stamp = utc_now();
        stamp[10] = ' ';
        stamp.pop_back();
        job["submitted_at"] = stamp;
        job["status"] = "parsed";
        Json rows = Json::array();
        DeviceQuery types;
        types.table = DeviceTable::TypePoint;
        types.filters = {{"type_code", owner.at("device_type_code")},
                         {"type_version", owner.at("type_version")}};
        types.limit = 100000;
        const auto definitions = request.rows(types);

        for (const auto& source : upload.parsed.at("rows")) {
            Json row = encode_record(CfgImportRow{});
            for (auto key_name : {"line_no", "vendor_key", "vendor_name", "semantic_code",
                                  "address_space", "address_offset", "wire_type", "byte_order",
                                  "scale_factor", "offset_value", "unit"})
                row[key_name] = source.at(key_name);
            row["import_job_id"] = id;
            row["target_device_path"] = owner.at("path");
            row["confirm_state"] = "none";
            const bool write = source.at("access") == "write";
            row["point_type"] = write ? "yt" : (source.at("wire_type") == "bool" ? "yx" : "yc");
            row["value_type"] = source.at("wire_type") == "bool" ? "bool" : "double";
            for (const auto& definition : definitions)
                if (definition.at("semantic_code") == source.at("semantic_code") &&
                    source.at("semantic_code") != "") {
                    row["point_type"] = definition.at("point_type");
                    row["value_type"] = definition.at("value_type");
                }
            if (body.at("address_base") == "one")
                row["address_offset"] = row.at("address_offset").get<int>() - 1;
            const auto wire = source.at("wire_type").get<std::string>();
            row["register_count"] = (wire == "i32" || wire == "u32" || wire == "f32") ? 2 : 1;
            rows.push_back(std::move(row));
        }
        if (!upload.parsed.at("issues").empty()) {
            // 无法无损解析的文件留作 failed 任务，不把丢失行包装成成功预览。
            job["status"] = "failed";
            job["error_code"] = "file_parse_failed";
            job["error_message"] = upload.parsed.at("issues").dump().substr(0, 1024);
        }
        rows = checked_rows(request, job, std::move(rows));
        save_preview(request, job, rows);
        return {detail(job, rows), Json::object(), 201};
    }

    auto job_id = text(body, "import_job_id");
    const bool edit = action == "update-row" || action == "validate" || action == "apply";
    auto job = request.one(DeviceTable::ImportJob, "import_job_id", job_id, edit);
    request.device(job.at("device_id"));
    if (action == "rows") {
        fields(body, {"import_job_id", "filters", "page", "page_size"});
        auto q = page_query(DeviceTable::ImportRow, body,
                            {"parse_status", "confirm_state", "point_type"});
        q.filters.push_back({"import_job_id", job_id});
        q.order_by = "line_no";
        auto result = request.query(q);
        for (auto& row : result["items"]) row = public_row(row);
        result["page"] = body.value("page", 1);
        result["page_size"] = q.limit;
        return {result};
    }
    auto rows = import_rows(request, job_id);
    if (action == "detail") {
        fields(body, {"import_job_id"});
        return {detail(job, rows)};
    }
    request.principal.require("import.write");

    if (action == "apply" && job.at("status") == "applied") {
        fields(body,
               {"import_job_id", "expected_preview_hash", "expected_config_revision", "mode"});
        DeviceResult result{detail(job, rows)};
        result.meta["replayed"] = true;
        return result;
    }
    if (job.at("status") == "applied" || job.at("status") == "failed")
        throw ApiError(409, "Conflict", "import_terminal");
    expect_preview(body, job, rows);
    if (action == "update-row") {
        fields(body, {"import_job_id", "line_no", "expected_preview_hash", "fields"});
        const int line = body.at("line_no").get<int>();
        auto found = std::find_if(rows.begin(), rows.end(),
                                  [&](const auto& row) { return row.at("line_no") == line; });
        if (found == rows.end()) throw ApiError(404, "NotFound", "import_row_not_found");
        const auto patch = body.at("fields");
        auto updated = merge_fields(
            *found, patch,
            {"vendor_key", "vendor_name", "point_type", "target_device_path", "semantic_code",
             "address_space", "address_offset", "bit_offset", "register_count",
             "protocol_address_json", "wire_type", "byte_order", "value_type", "scale_factor",
             "offset_value", "unit", "confirm_state", "confirm_note"});
        if (patch.size() > (patch.count("confirm_state") + patch.count("confirm_note")))
            updated["confirm_state"] = "none";
        if (updated.at("confirm_state") != "none" && updated.at("confirm_state") != "accepted" &&
            updated.at("confirm_state") != "fixed" && updated.at("confirm_state") != "rejected")
            throw ApiError(422, "InvalidArgument", "invalid_confirmation_state");
        if (updated.at("confirm_state") != "none") {
            text(updated, "confirm_note", 512);
            updated["confirm_by"] = request.principal.subject_id;
            auto stamp = utc_now();
            stamp[10] = ' ';
            stamp.pop_back();
            updated["confirm_at"] = stamp;
        }
        *found = updated;
        job["status"] = "parsed";
        rows = checked_rows(request, job, std::move(rows));
        save_preview(request, job, rows);
        return {detail(job, rows)};
    }
    if (action == "validate") {
        fields(body, {"import_job_id", "expected_preview_hash"});
        rows = checked_rows(request, job, std::move(rows));
        save_preview(request, job, rows);
        return {detail(job, rows)};
    }
    if (action == "apply") {
        fields(body,
               {"import_job_id", "expected_preview_hash", "expected_config_revision", "mode"});
        request.principal.require("point.write");
        request.expect_revision(body);
        if (text(body, "mode") != "merge")
            throw ApiError(422, "Unsupported", "import_mode_unsupported");
        rows = checked_rows(request, job, std::move(rows));
        if (job.at("status") != "confirmed")
            throw ApiError(422, "InvalidArgument", "import_confirmation_required");
        int created = 0, updated = 0, excluded = 0;
        for (auto& row : rows) {
            if (row.at("confirm_state") == "rejected") {
                ++excluded;
                continue;
            }
            auto target = request.one(DeviceTable::Device, "path", row.at("target_device_path"));
            auto kind = row.at("point_type").get<std::string>();
            auto table = point_table(kind);
            DeviceQuery lookup;
            lookup.table = table;
            lookup.limit = 1;
            lookup.filters = {{"binding_id", job.at("binding_id")},
                              {"vendor_key", row.at("vendor_key")}};
            auto old = request.rows(lookup);
            auto point_id = old.empty() ? "pt-" + random_token().substr(0, 40)
                                        : old.front().at("point_id").get<std::string>();
            if (!old.empty() && old.front().at("owner_device_id") != target.at("device_id"))
                throw ApiError(409, "Conflict", "import_point_owner_changed");
            auto semantic = row.at("semantic_code").get<std::string>();
            Json patch = {{"binding_id", job.at("binding_id")},
                          {"name", row.at("vendor_name")},
                          {"semantic_code", semantic},
                          {"vendor_key", row.at("vendor_key")},
                          {"wire_type", row.at("wire_type")},
                          {"byte_order", row.at("byte_order")},
                          {"value_type", row.at("value_type")},
                          {"unit", row.at("unit")},
                          {"address",
                           {{"space", row.at("address_space")},
                            {"offset", row.at("address_offset")},
                            {"bit_offset", row.at("bit_offset")},
                            {"register_count", row.at("register_count")}}}};
            if (kind == "yc" || kind == "yt") {
                patch["scale_factor"] = row.at("scale_factor");
                patch["offset_value"] = row.at("offset_value");
            }
            if (old.empty()) {
                patch["code"] = semantic.empty()
                                    ? "vendor_" + sha256(row.at("vendor_key")).substr(0, 24)
                                    : semantic;
                patch["enabled"] = kind == "yc" || kind == "yx";
                if (kind == "yk" || kind == "yt")
                    patch["disabled_reason"] = "导入后需补齐写入及反馈规则";
                patch["origin"] = "vendor";
            }
            Json input = {{"device_id", target.at("device_id")},
                          {"point_type", kind},
                          {"point_id", point_id},
                          {"expected_config_revision", body.at("expected_config_revision")},
                          {"fields", patch}};
            point_action(request, old.empty() ? "/api/points/create" : "/api/points/update", input);
            auto persisted = request.one(table, "point_id", point_id);
            persisted["source_import_row_id"] =
                job_id + ":" + std::to_string(row.at("line_no").get<int>());
            request.put(table, persisted);
            row["resolved_point_id"] = point_id;
            if (old.empty())
                ++created;
            else
                ++updated;
        }
        job["status"] = "applied";
        auto stamp = utc_now();
        stamp[10] = ' ';
        stamp.pop_back();
        job["finished_at"] = stamp;
        save_preview(request, job, rows);
        return {{{"import_job_id", job_id},
                 {"status", "applied"},
                 {"created", created},
                 {"updated", updated},
                 {"excluded", excluded}}};
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
