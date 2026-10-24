/**
 * @file station_import_actions.cpp
 * @brief 整站点表打包导入：一次上传 manifest.json + N 个设备 Excel。
 *
 * 点表 Excel 只有点；层级关系与每设备通讯参数在 manifest.json 中声明。
 * 编排在一个工程事务内完成：校验 manifest → 建设备树 → 建通道与绑定 →
 * 逐设备走既有单设备导入（cfg_import_job 逐行确认）。所有稳定 ID 由
 * 包摘要确定性派生，重复上传同一包即精确重放；确认与落库仍走既有
 * update-row/validate/apply 流程，整包不会自动 apply。
 */
#include "web/modules/device/infrastructure/point_table_parser.h"
#include "web/modules/device/infrastructure/station_package_parser.h"
#include "web/platform/crypto.h"
#include "web/transport/json.h"

namespace ems::web {
using namespace database;

namespace {

/// 由包摘要 + 业务键派生稳定 ID；同一包重复上传得到相同 ID，创建动作精确重放。
std::string derived_id(const std::string& prefix, const std::string& package,
                       const std::string& key) {
    return prefix + sha256(package + "|" + key).substr(0, 40);
}

Json with_revision(const std::string& revision, Json body) {
    body["expected_config_revision"] = revision;
    return body;
}

/// 解析点表条目的绑定：显式 binding_code 或该设备唯一的采集绑定。
const Json& resolve_binding(const Json& entry, const std::string& device_code, const Json& bindings,
                            const std::string& explicit_code) {
    const Json* found = nullptr;
    int collect_bindings = 0;
    for (const auto& binding : bindings) {
        if (binding.at("device_code") != device_code) continue;
        const bool collect = binding.value("can_collect", 0) != 0;
        if (!explicit_code.empty()) {
            if (binding.at("channel_code") == explicit_code && collect) {
                if (found != nullptr)
                    throw ApiError(422, "InvalidArgument", "station_binding_ambiguous");
                found = &binding;
            }
            continue;
        }
        if (collect) {
            ++collect_bindings;
            found = &binding;
        }
    }
    if (found == nullptr || (!explicit_code.empty() ? false : collect_bindings != 1))
        throw ApiError(422, "InvalidArgument", "station_binding_ambiguous");
    (void)entry;
    return *found;
}

}  // namespace

DeviceResult station_import_action(DeviceRequest& request, const std::string& path,
                                   const Json& body, const Upload& upload,
                                   const std::string& workspace) {
    const auto action = path.substr(path.find_last_of('/') + 1);
    if (action == "template") {
        // 返回 manifest 模板，供前端生成下载与填写提示。
        return {Json{{"schema", "ems-station-package/1"},
                     {"station", {{"code", "station001"}}},
                     {"devices", Json::array({Json{{"code", "unit01"},
                                                   {"name", "储能单元 01"},
                                                   {"node_kind", "unit"},
                                                   {"parent_code", ""}},
                                              Json{{"code", "pcs01"},
                                                   {"name", "PCS-1"},
                                                   {"node_kind", "device"},
                                                   {"parent_code", "unit01"},
                                                   {"device_type_code", "pcs"},
                                                   {"type_version", "v1"},
                                                   {"vendor", "示例厂商"},
                                                   {"model", "PCS-125K"},
                                                   {"firmware_version", "FW2.1"},
                                                   {"rated_power_kw", 125.0}}})},
                     {"channels", Json::array({Json{{"code", "rs48501"},
                                                    {"name", "PCS 通道"},
                                                    {"protocol", "modbus"},
                                                    {"conn_mode", "tcp_client"},
                                                    {"endpoint", "192.168.1.10:502"},
                                                    {"poll_interval_ms", 1000},
                                                    {"timeout_ms", 3000},
                                                    {"retry_times", 1},
                                                    {"max_sessions", 1}}})},
                     {"bindings", Json::array({Json{{"device_code", "pcs01"},
                                                    {"channel_code", "rs48501"},
                                                    {"device_address", "1"},
                                                    {"profile_version", ""},
                                                    {"can_collect", 1},
                                                    {"can_control", 1},
                                                    {"can_forward", 0}}})},
                     {"point_files", Json::array({Json{{"device_code", "pcs01"},
                                                       {"file", "pcs01.xlsx"},
                                                       {"address_base", "zero"}}})}}};
    }
    if (action != "create") throw ApiError(404, "NotFound", "route_not_found");
    if (!upload.parsed.is_object() || !upload.package)
        throw ApiError(500, "Internal", "package_not_preparsed");
    // 整站导入跨越全部设备子树：限定子树的账号不能执行。
    if (!request.principal.device_roots.count("*"))
        throw ApiError(403, "PermissionDenied", "unscoped_station_import");
    request.principal.require("device.write");
    request.principal.require("channel.write");
    request.principal.require("binding.write");
    request.principal.require("import.write");

    const StationPackage& package = *upload.package;
    const Json& manifest = package.manifest;
    const auto station_code = text(manifest.at("station"), "code");
    if (request.station.at("code") != station_code)
        throw ApiError(422, "InvalidArgument", "station_code_mismatch");
    // 整个包在一个工程事务内落地：先建树和通讯参数，再逐设备生成待确认导入任务。
    const std::string revision =
        std::to_string(request.station.at("config_revision").get<std::int64_t>());

    std::map<std::string, std::string> device_ids;   // code → 稳定设备 ID
    std::map<std::string, std::string> channel_ids;  // code → 稳定通道 ID
    std::map<std::string, std::string> binding_ids;  // "dev|chan" → 稳定绑定 ID
    const auto& bindings = manifest.at("bindings");

    for (const auto& device : manifest.at("devices")) {
        const auto code = device.at("code").get<std::string>();
        const auto kind = device.at("node_kind").get<std::string>();
        const auto id = derived_id("dev-", package.package_sha256, code);
        const auto parent = device.value("parent_code", std::string{}).empty()
                                ? request.station.at("device_id").get<std::string>()
                                : device_ids.at(device.at("parent_code").get<std::string>());
        Json fields = {{"parent_device_id", parent},
                       {"node_kind", kind},
                       {"code", code},
                       {"name", device.value("name", code)},
                       {"enabled", device.value("enabled", 1)}};
        if (kind != "unit") {
            fields["device_type_code"] = device.at("device_type_code");
            fields["type_version"] = device.at("type_version");
            for (auto key : {"vendor", "model", "firmware_version"})
                if (device.contains(key)) fields[key] = device.at(key);
            if (device.contains("rated_power_kw"))
                fields["rated_power_kw"] = device.at("rated_power_kw");
        }
        if (kind == "component")
            fields["source_device_id"] =
                device_ids.at(device.at("source_device_code").get<std::string>());
        device_action(request, "/api/devices/create",
                      {{"device_id", id},
                       {"expected_config_revision", revision},
                       {"fields", std::move(fields)}});
        device_ids.emplace(code, id);
    }

    for (const auto& channel : manifest.at("channels")) {
        const auto code = channel.at("code").get<std::string>();
        const auto id = derived_id("ch-", package.package_sha256, code);
        Json fields = {{"code", code},
                       {"name", channel.value("name", code)},
                       {"protocol", channel.at("protocol")},
                       {"conn_mode", channel.at("conn_mode")},
                       {"endpoint", channel.value("endpoint", std::string{})},
                       {"enabled", channel.value("enabled", 1)}};
        for (auto key : {"poll_interval_ms", "timeout_ms", "retry_times", "max_sessions"})
            if (channel.contains(key)) fields[key] = channel.at(key);
        binding_action(request, "/api/channels/create",
                       {{"channel_id", id},
                        {"expected_config_revision", revision},
                        {"fields", std::move(fields)}});
        channel_ids.emplace(code, id);
    }

    for (const auto& binding : bindings) {
        const auto key = binding.at("device_code").get<std::string>() + "|" +
                         binding.at("channel_code").get<std::string>();
        const auto id = derived_id("bnd-", package.package_sha256, key);
        Json fields = {
            {"channel_id", channel_ids.at(binding.at("channel_code").get<std::string>())},
            {"device_address", binding.at("device_address")},
            {"enabled", binding.value("enabled", 1)}};
        for (auto key_name :
             {"profile_version", "can_collect", "can_control", "can_forward", "description"})
            if (binding.contains(key_name)) fields[key_name] = binding.at(key_name);
        binding_action(request, "/api/bindings/create",
                       {{"device_id", device_ids.at(binding.at("device_code").get<std::string>())},
                        {"binding_id", id},
                        {"expected_config_revision", revision},
                        {"fields", std::move(fields)}});
        binding_ids.emplace(key, id);
    }

    Json jobs = Json::array();
    for (const auto& entry : manifest.at("point_files")) {
        const auto device_code = entry.at("device_code").get<std::string>();
        const auto device_id = device_ids.at(device_code);
        const auto& binding = resolve_binding(entry, device_code, bindings,
                                              entry.value("binding_code", std::string{}));
        const auto binding_id =
            binding_ids.at(device_code + "|" + binding.at("channel_code").get<std::string>());
        Upload file;
        file.filename = entry.at("file").get<std::string>();
        file.bytes = package.point_files.at(device_code);
        try {
            file.parsed = parse_vendor_xlsx(file.bytes, workspace);
        } catch (const ApiError&) {
            throw ApiError(422, "InvalidArgument", "station_point_file_invalid", "NotApplied",
                           Json::array({{{"device_code", device_code}, {"file", file.filename}}}));
        }
        DeviceResult created = import_action(request, "/api/point-imports/create",
                                             {{"device_id", device_id},
                                              {"binding_id", binding_id},
                                              {"source_kind", "vendor_xlsx"},
                                              {"address_base", entry.at("address_base")}},
                                             file, workspace);
        Json summary = {{"device_code", device_code},
                        {"device_id", device_id},
                        {"binding_id", binding_id},
                        {"import_job_id", created.data.at("import_job_id")},
                        {"status", created.data.at("status")},
                        {"row_total", created.data.value("row_total", 0)},
                        {"row_ok", created.data.value("row_ok", 0)},
                        {"row_warning", created.data.value("row_warning", 0)},
                        {"row_error", created.data.value("row_error", 0)},
                        {"preview_hash", created.data.value("preview_hash", std::string{})}};
        jobs.push_back(std::move(summary));
    }

    return {{{"station_code", station_code},
             {"devices_created", device_ids.size()},
             {"channels_created", channel_ids.size()},
             {"bindings_created", binding_ids.size()},
             {"import_jobs", std::move(jobs)},
             {"next_step", "逐设备确认导入任务：point-imports/update-row → validate → apply"}},
            Json::object(),
            201};
}

}  // namespace ems::web
