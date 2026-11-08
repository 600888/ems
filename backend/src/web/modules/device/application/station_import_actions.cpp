/**
 * @file station_import_actions.cpp
 * @brief 整站点表打包导入：一次上传 manifest.json + N 个设备 Excel。
 *
 * 点表 Excel 只有点；层级关系与每设备通讯参数在 manifest.json 中声明。
 * 编排在一个工程事务内完成：校验 manifest → 建设备树 → 建通道与绑定 →
 * 逐设备解析并直接应用四遥点表。设备、通道及绑定 ID 由包摘要派生，
 * 重复上传同一包即精确重放。导入任务保留来源记录，整站只提交一次
 * 并递增一次配置计数。
 */
#include <algorithm>

#include "web/modules/device/infrastructure/point_table_parser.h"
#include "web/modules/device/infrastructure/station_package_parser.h"
#include "web/platform/crypto.h"
#include "web/transport/json.h"

namespace ems::web {
using namespace database;

namespace {
bool manifest_flag(const Json& item, const char* key, bool fallback) {
    if (!item.contains(key)) return fallback;
    const auto& value = item.at(key);
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer() && (value == 0 || value == 1)) return value == 1;
    throw ApiError(422, "InvalidArgument", "manifest_boolean_required");
}

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
        const bool collect = manifest_flag(binding, "can_collect", false);
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

void prepare_station_point_files(StationPackage& package, const std::string& workspace) {
    const auto& manifest = package.manifest;
    for (const auto& entry : manifest.at("point_files")) {
        const auto code = entry.at("device_code").get<std::string>();
        const auto& binding = resolve_binding(entry, code, manifest.at("bindings"),
                                              entry.value("binding_code", std::string{}));
        const auto channel = std::find_if(
            manifest.at("channels").begin(), manifest.at("channels").end(),
            [&](const Json& item) { return item.at("code") == binding.at("channel_code"); });
        const auto file = entry.at("file").get<std::string>();
        const auto protocol = channel->at("protocol").get<std::string>();
        const auto key = std::make_pair(file, protocol);
        if (package.parsed_point_files.count(key)) continue;
        try {
            package.parsed_point_files.emplace(
                key, parse_vendor_xlsx(package.point_files.at(file), workspace, protocol));
        } catch (const ApiError&) {
            throw ApiError(422, "InvalidArgument", "station_point_file_invalid", "NotApplied",
                           Json::array({{{"device_code", code}, {"file", file}}}));
        }
    }
}

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
                     {"point_files",
                      Json::array({Json{{"device_code", "pcs01"}, {"file", "pcs01.xlsx"}}})}}};
    }
    if (action != "create" && action != "inspect")
        throw ApiError(404, "NotFound", "route_not_found");
    if (!upload.package) throw ApiError(500, "Internal", "package_not_preparsed");
    // 整站导入跨越全部设备子树：限定子树的账号不能执行。
    if (!request.principal.device_roots.count("*"))
        throw ApiError(403, "PermissionDenied", "unscoped_station_import");
    request.principal.require("device.write");
    request.principal.require("channel.write");
    request.principal.require("binding.write");
    request.principal.require("import.write");
    request.principal.require("point.write");

    const StationPackage& package = *upload.package;
    const Json& manifest = package.manifest;
    const auto package_station_code = text(manifest.at("station"), "code");
    const auto station_code = request.station.at("code").get<std::string>();
    const bool station_code_match = station_code == package_station_code;
    Json warnings = Json::array();
    if (!station_code_match)
        warnings.push_back({{"code", "station_code_mismatch"},
                            {"package_station_code", package_station_code},
                            {"station_code", station_code}});
    if (action == "inspect")
        return {{{"station_code", station_code},
                 {"package_station_code", package_station_code},
                 {"station_code_match", station_code_match},
                 {"warnings", std::move(warnings)}}};
    // 整个包在一个工程事务内落地：设备、通讯参数及全部四遥点共同保存。
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
                       {"enabled", manifest_flag(device, "enabled", true)}};
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
                       {"enabled", manifest_flag(channel, "enabled", true)}};
        for (auto key : {"poll_interval_ms", "timeout_ms", "retry_times", "max_sessions"})
            if (channel.contains(key)) fields[key] = channel.at(key);
        if (channel.contains("serial")) fields["serial"] = channel.at("serial");
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
            {"enabled", manifest_flag(binding, "enabled", true)}};
        for (auto key_name :
             {"profile_version", "can_collect", "can_control", "can_forward", "description"})
            if (binding.contains(key_name)) fields[key_name] = binding.at(key_name);
        for (auto key_name : {"can_collect", "can_control", "can_forward"})
            if (binding.contains(key_name))
                fields[key_name] = manifest_flag(binding, key_name, false);
        binding_action(request, "/api/bindings/create",
                       {{"device_id", device_ids.at(binding.at("device_code").get<std::string>())},
                        {"binding_id", id},
                        {"expected_config_revision", revision},
                        {"fields", std::move(fields)}});
        binding_ids.emplace(key, id);
    }

    Json jobs = Json::array();
    int points_created = 0, points_updated = 0, points_total = 0;
    for (const auto& entry : manifest.at("point_files")) {
        const auto device_code = entry.at("device_code").get<std::string>();
        const auto device_id = device_ids.at(device_code);
        const auto& binding = resolve_binding(entry, device_code, bindings,
                                              entry.value("binding_code", std::string{}));
        const auto binding_id =
            binding_ids.at(device_code + "|" + binding.at("channel_code").get<std::string>());
        Upload file;
        file.filename = entry.at("file").get<std::string>();
        file.bytes = package.point_files.at(file.filename);
        const auto channel = std::find_if(
            manifest.at("channels").begin(), manifest.at("channels").end(),
            [&](const Json& item) { return item.at("code") == binding.at("channel_code"); });
        file.parsed = package.parsed_point_files.at(
            {file.filename, channel->at("protocol").get<std::string>()});
        const auto target_code = entry.value("target_device_code", device_code);
        if (target_code != device_code) {
            const auto target = request.device(device_ids.at(target_code));
            for (auto& row : file.parsed["rows"]) row["target_device_path"] = target.at("path");
        }
        DeviceResult created =
            import_action(request, "/api/point-imports/create",
                          {{"device_id", device_id},
                           {"binding_id", binding_id},
                           {"source_kind", file.parsed.value("source_kind", "vendor_xlsx")}},
                          file, workspace, true);
        if (created.data.at("status") == "failed" || created.data.value("row_error", 0) != 0)
            throw ApiError(422, "InvalidArgument", "station_point_file_invalid", "NotApplied",
                           Json::array({{{"device_code", device_code}, {"file", file.filename}}}));
        const auto applied =
            import_action(request, "/api/point-imports/apply",
                          {{"import_job_id", created.data.at("import_job_id")},
                           {"expected_preview_hash", created.data.at("preview_hash")},
                           {"expected_config_revision", revision},
                           {"mode", "merge"}},
                          Upload{}, workspace, true);
        points_created += applied.data.value("created", 0);
        points_updated += applied.data.value("updated", 0);
        points_total += created.data.value("row_total", 0) - applied.data.value("excluded", 0);
        Json summary = {{"device_code", device_code},
                        {"target_device_code", target_code},
                        {"target_device_id", device_ids.at(target_code)},
                        {"device_id", device_id},
                        {"binding_id", binding_id},
                        {"import_job_id", created.data.at("import_job_id")},
                        {"status", applied.data.at("status")},
                        {"created", applied.data.value("created", 0)},
                        {"updated", applied.data.value("updated", 0)},
                        {"row_total", created.data.value("row_total", 0)},
                        {"row_ok", created.data.value("row_ok", 0)},
                        {"row_warning", created.data.value("row_warning", 0)},
                        {"row_error", created.data.value("row_error", 0)},
                        {"preview_hash", applied.data.value("preview_hash", std::string{})}};
        jobs.push_back(std::move(summary));
    }

    return {{{"station_code", station_code},
             {"package_station_code", package_station_code},
             {"station_code_match", station_code_match},
             {"warnings", std::move(warnings)},
             {"devices_created", device_ids.size()},
             {"channels_created", channel_ids.size()},
             {"bindings_created", binding_ids.size()},
             {"points_created", points_created},
             {"points_updated", points_updated},
             {"points_total", points_total},
             {"import_jobs", std::move(jobs)},
             {"next_step", "设备与点位已保存生效"}},
            Json::object(),
            201};
}

}  // namespace ems::web
