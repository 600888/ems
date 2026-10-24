/**
 * @file station_package_parser.cpp
 * @brief 整站点表导入包解析：zip 解包与 manifest.json 结构校验。
 *
 * 包格式（ems-station-package/1）：manifest.json 描述设备树层级、每设备
 * 通讯参数（通道 + 绑定）与点表文件映射；每个需要导点的设备附带一个
 * xlsx 点表（列格式与单设备 vendor_xlsx 导入完全一致）。
 * 层级与通讯参数是点表 Excel 携带不了的信息，由 manifest 统一声明。
 */
#include "station_package_parser.h"

#include <miniz.h>

#include <filesystem>
#include <set>

#include "web/platform/crypto.h"
#include "web/transport/json.h"

namespace ems::web {
namespace {

constexpr std::size_t kMaxManifestBytes = 1 * 1024 * 1024;
constexpr std::size_t kMaxPointFileBytes = 2 * 1024 * 1024;  // 与单文件导入一致
constexpr std::uint64_t kMaxExpandedBytes = 100ULL * 1024 * 1024;
constexpr std::size_t kMaxEntries = 512;
constexpr std::size_t kMaxCodeLength = 64;

[[noreturn]] void reject(const char* reason) { throw ApiError(422, "InvalidArgument", reason); }

/// 设备/通道 code 规则与界面创建一致：字母数字下划线连字符。
bool valid_code(const std::string& code) {
    if (code.empty() || code.size() > kMaxCodeLength) return false;
    return code.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
           std::string::npos;
}

const Json& array_field(const Json& manifest, const char* key) {
    if (!manifest.contains(key) || !manifest.at(key).is_array()) reject("manifest_array_required");
    return manifest.at(key);
}

std::string require_code(const Json& item, const char* key) {
    if (!item.contains(key) || !item.at(key).is_string()) reject("manifest_code_required");
    auto code = item.at(key).get<std::string>();
    if (!valid_code(code)) reject("manifest_code_invalid");
    return code;
}

/// 校验设备树：引用闭合（指向已定义节点）+ 单元必须挂站根 + 部件必须带采集设备。
void validate_devices(const Json& manifest, std::map<std::string, const Json*>& by_code) {
    const auto& devices = array_field(manifest, "devices");
    if (devices.empty()) reject("manifest_devices_required");
    std::set<std::string> seen;
    for (const auto& device : devices) {
        if (!device.is_object()) reject("manifest_object_required");
        const auto code = require_code(device, "code");
        if (!seen.insert(code).second) reject("manifest_code_duplicate");
        const auto kind = text(device, "node_kind");
        if (kind != "unit" && kind != "device" && kind != "component")
            reject("manifest_node_kind_invalid");
        const auto parent = device.contains("parent_code") && device.at("parent_code").is_string()
                                ? device.at("parent_code").get<std::string>()
                                : std::string{};
        // device/component 的 parent_code 为空表示直接挂站根（站级公共设备，如关口电表）。
        if (kind == "unit" ? !parent.empty() : (!parent.empty() && !by_code.count(parent)))
            reject("manifest_parent_invalid");
        if (kind != "unit") {
            if (text(device, "device_type_code").empty() || text(device, "type_version").empty())
                reject("manifest_device_type_required");
        }
        if (kind == "component") {
            const auto source = text(device, "source_device_code");
            if (source.empty() || !by_code.count(source)) reject("manifest_source_invalid");
        }
        text(device, "name", 128);
        by_code.emplace(code, &device);
    }
}

/// 校验通道与绑定：协议白名单、引用闭合、绑定目标必须是通信设备。
void validate_connections(const Json& manifest, const std::map<std::string, const Json*>& devices,
                          std::map<std::string, const Json*>& channels_by_code) {
    for (const auto& channel : array_field(manifest, "channels")) {
        if (!channel.is_object()) reject("manifest_object_required");
        const auto code = require_code(channel, "code");
        if (!channels_by_code.emplace(code, &channel).second) reject("manifest_code_duplicate");
        const auto protocol = text(channel, "protocol");
        if (protocol != "modbus" && protocol != "iec104" && protocol != "dlt645")
            reject("manifest_protocol_invalid");
        const auto mode = text(channel, "conn_mode");
        if (mode != "tcp_client" && mode != "tcp_server" && mode != "serial" && mode != "udp")
            reject("manifest_conn_mode_invalid");
        if (text(channel, "endpoint", 255).empty()) reject("manifest_endpoint_required");
    }
    std::set<std::string> pairs;
    for (const auto& binding : array_field(manifest, "bindings")) {
        if (!binding.is_object()) reject("manifest_object_required");
        const auto device_code = require_code(binding, "device_code");
        const auto channel_code = require_code(binding, "channel_code");
        const auto device = devices.find(device_code);
        if (device == devices.end() ||
            device->second->at("node_kind").get<std::string>() != "device")
            reject("manifest_binding_device_invalid");
        if (!channels_by_code.count(channel_code)) reject("manifest_channel_invalid");
        if (text(binding, "device_address").empty()) reject("manifest_address_required");
        if (!pairs.insert(device_code + "|" + channel_code).second)
            reject("manifest_binding_duplicate");
    }
}

/// 校验点表文件：目标为设备、绑定归属一致、文件存在。
void validate_point_files(const Json& manifest, const std::map<std::string, const Json*>& devices,
                          const std::map<std::string, const Json*>& channels) {
    for (const auto& entry : array_field(manifest, "point_files")) {
        if (!entry.is_object()) reject("manifest_object_required");
        const auto device_code = require_code(entry, "device_code");
        const auto device = devices.find(device_code);
        if (device == devices.end() ||
            device->second->at("node_kind").get<std::string>() != "device")
            reject("manifest_point_device_invalid");
        const auto base = text(entry, "address_base");
        if (base != "zero" && base != "one") reject("address_base_invalid");
        if (!entry.contains("file") || !entry.at("file").is_string())
            reject("manifest_file_required");
        const bool has_binding =
            entry.contains("binding_code") && entry.at("binding_code").is_string();
        const auto binding =
            has_binding ? entry.at("binding_code").get<std::string>() : std::string{};
        if (!binding.empty() && !channels.count(binding)) reject("manifest_channel_invalid");
    }
}

}  // namespace

StationPackage parse_station_package(const Upload& upload) {
    if (upload.bytes.empty()) reject("package_empty");
    // 在解包前核对 ZIP 目录的总解压预算，阻止压缩炸弹（与 xlsx 检查一致）。
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, upload.bytes.data(), upload.bytes.size(), 0))
        reject("invalid_package_zip");
    std::uint64_t expanded = 0;
    bool safe = mz_zip_reader_get_num_files(&zip) <= kMaxEntries;
    std::map<std::string, std::string> entries;
    for (mz_uint i = 0; safe && i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            safe = false;
            break;
        }
        // 只接受包根下的普通文件名：拒绝目录穿越与子目录，防止 zip slip。
        std::string name(stat.m_filename);
        if (stat.m_is_directory || name.find('/') != std::string::npos ||
            name.find('\\') != std::string::npos || name.find("..") != std::string::npos ||
            name.size() > 255 || stat.m_uncomp_size > kMaxPointFileBytes) {
            safe = false;
            break;
        }
        expanded += stat.m_uncomp_size;
        safe = expanded <= kMaxExpandedBytes;
        if (safe) {
            std::string bytes(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
            if (!mz_zip_reader_extract_to_mem(&zip, i, bytes.data(), bytes.size(), 0)) {
                safe = false;
                break;
            }
            entries.emplace(std::move(name), std::move(bytes));
        }
    }
    mz_zip_reader_end(&zip);
    if (!safe) throw ApiError(413, "ResourceExhausted", "package_expanded_limit");

    const auto manifest_entry = entries.find("manifest.json");
    if (manifest_entry == entries.end() || manifest_entry->second.size() > kMaxManifestBytes)
        reject("manifest_missing");
    Json manifest = Json::parse(manifest_entry->second, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) reject("manifest_json_invalid");
    if (text(manifest, "schema") != "ems-station-package/1") reject("manifest_schema_unsupported");
    if (!manifest.contains("station") || !manifest.at("station").is_object() ||
        !valid_code(text(manifest.at("station"), "code")))
        reject("manifest_station_invalid");
    if (!array_field(manifest, "channels").empty() && !manifest.contains("bindings"))
        reject("manifest_bindings_required");

    std::map<std::string, const Json*> devices;
    validate_devices(manifest, devices);
    std::map<std::string, const Json*> channels;
    validate_connections(manifest, devices, channels);
    validate_point_files(manifest, devices, channels);

    StationPackage package;
    package.manifest = std::move(manifest);
    for (const auto& entry : package.manifest.at("point_files")) {
        auto file = entry.at("file").get<std::string>();
        const auto found = entries.find(file);
        if (found == entries.end() || !valid_code(file.substr(0, file.size() - 5)) ||
            file.size() <= 5 || file.substr(file.size() - 5) != ".xlsx")
            reject("manifest_file_missing");
        if (!package.point_files.emplace(entry.at("device_code").get<std::string>(), found->second)
                 .second)
            reject("manifest_file_duplicate");
    }
    package.package_sha256 = sha256(upload.bytes);
    return package;
}

}  // namespace ems::web
