#include <common/database/configuration/device_management_validation.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>

namespace ems::database {
std::vector<DeviceFinding> validate_device_management(
    const DeviceConfigRecords& records, const std::vector<std::string>& confirmed_import_points) {
    std::vector<DeviceFinding> issues;
    std::unordered_map<std::string, const CfgDeviceRow*> nodes;
    std::unordered_map<std::string, const CfgBindingRow*> bindings;
    std::unordered_map<std::string, const CfgChannelRow*> channels;
    std::set<std::string> paths, siblings, binding_pairs, ids, codes, vendor_keys, semantic_keys;

    auto problem = [&](std::string reason, std::string device = {}, std::string point = {}) {
        if (issues.size() < 1000)
            issues.push_back({std::move(reason), std::move(device), std::move(point)});
    };

    // 先建立身份索引，后续检查不依赖记录读取顺序。
    for (const auto& node : records.devices) {
        if (!nodes.emplace(node.device_id, &node).second || node.device_id.empty())
            problem("device_id_conflict", node.device_id);
        if (!paths.insert(node.path).second ||
            !siblings.insert(node.parent_device_id + "\n" + node.code).second)
            problem("device_path_or_code_conflict", node.device_id);
    }
    for (const auto& channel : records.channels) channels.emplace(channel.channel_id, &channel);
    for (const auto& binding : records.bindings) bindings.emplace(binding.binding_id, &binding);

    int stations = 0;
    for (const auto& node : records.devices) {
        if (node.node_kind == "station") {
            ++stations;
            if (!node.parent_device_id.empty() || node.path != "/" || node.depth != 0)
                problem("invalid_station", node.device_id);
            continue;
        }
        auto parent = nodes.find(node.parent_device_id);
        if (parent == nodes.end()) {
            problem("parent_not_found", node.device_id);
            continue;
        }
        auto p = parent->second;
        if (node.depth != p->depth + 1 || node.depth > 8 ||
            node.path != (p->path == "/" ? "/" : p->path + "/") + node.code)
            problem("invalid_device_path", node.device_id);

        const bool organization = node.node_kind == "unit" && p->node_kind == "station";
        const bool device =
            node.node_kind == "device" && (p->node_kind == "station" || p->node_kind == "unit");
        const bool component = node.node_kind == "component" &&
                               (p->node_kind == "device" || p->node_kind == "component");
        if (!organization && !device && !component) problem("invalid_parent_kind", node.device_id);

        std::set<std::string> visited;
        auto cursor = &node;
        for (int depth = 0; cursor && depth <= 9; ++depth) {
            if (!visited.insert(cursor->device_id).second) {
                problem("device_cycle", node.device_id);
                break;
            }
            auto next = nodes.find(cursor->parent_device_id);
            cursor = next == nodes.end() ? nullptr : next->second;
        }
        if (device || component) {
            auto type = std::find_if(records.device_types.begin(), records.device_types.end(),
                                     [&](const auto& t) {
                                         return t.type_code == node.device_type_code &&
                                                t.type_version == node.type_version;
                                     });
            if (type == records.device_types.end())
                problem("device_type_not_found", node.device_id);
        }
        if (component) {
            auto source = nodes.find(node.source_device_id);
            if (source == nodes.end() || source->second->node_kind != "device" ||
                !visited.count(node.source_device_id))
                problem("invalid_source_device", node.device_id);
        } else if (!node.source_device_id.empty())
            problem("unexpected_source_device", node.device_id);
    }
    if (stations != 1) problem("unique_station_required");

    for (const auto& channel : records.channels) {
        if (channel.timeout_ms <= 0 || channel.poll_interval_ms <= 0 || channel.retry_times < 0 ||
            channel.retry_times > 10 || channel.max_sessions < 1)
            problem("invalid_channel_budget");
    }
    for (const auto& binding : records.bindings) {
        auto owner = nodes.find(binding.device_id);
        if (owner == nodes.end() || owner->second->node_kind != "device")
            problem("binding_owner_invalid", binding.device_id);
        if (!channels.count(binding.channel_id)) problem("channel_not_found", binding.device_id);
        if (!binding.can_collect && !binding.can_control && !binding.can_forward)
            problem("binding_role_required", binding.device_id);
        if (!binding_pairs.insert(binding.device_id + "\n" + binding.channel_id).second)
            problem("binding_pair_conflict", binding.device_id);
    }

    struct Address {
        std::string physical, device, point, group;
        int begin, end, bit;
        bool write;
    };

    std::vector<Address> addresses;
    std::unordered_map<std::string, std::pair<std::string, std::string>> point_owners;
    auto visit = [&](const auto& points, const std::string& kind) {
        for (const auto& point : points) {
            point_owners[point.point_id] = {point.owner_device_id, kind};
            if (!ids.insert(point.point_id).second || point.point_id.empty())
                problem("point_id_conflict", point.owner_device_id, point.point_id);
            if (!codes.insert(point.owner_device_id + "\n" + point.code).second)
                problem("point_code_conflict", point.owner_device_id, point.point_id);
            if (!point.vendor_key.empty() &&
                !vendor_keys.insert(point.binding_id + "\n" + point.vendor_key).second)
                problem("vendor_key_conflict", point.owner_device_id, point.point_id);
            if (!point.semantic_code.empty()) {
                if (!semantic_keys.insert(point.owner_device_id + "\n" + point.semantic_code)
                         .second)
                    problem("semantic_code_conflict", point.owner_device_id, point.point_id);
                auto owner = nodes.find(point.owner_device_id);
                const auto type = std::find_if(
                    records.type_points.begin(), records.type_points.end(), [&](const auto& t) {
                        return owner != nodes.end() &&
                               t.type_code == owner->second->device_type_code &&
                               t.type_version == owner->second->type_version &&
                               t.semantic_code == point.semantic_code && t.point_type == kind;
                    });
                if (type == records.type_points.end())
                    problem("point_type_mismatch", point.owner_device_id, point.point_id);
            }
            auto owner = nodes.find(point.owner_device_id);
            auto binding = bindings.find(point.binding_id);
            if (owner == nodes.end() || binding == bindings.end()) {
                problem("point_reference_missing", point.owner_device_id, point.point_id);
                continue;
            }
            auto node = owner->second;
            auto route = binding->second;
            auto source = node->node_kind == "component" ? node->source_device_id : node->device_id;
            if (route->device_id != source ||
                (node->node_kind != "device" && node->node_kind != "component"))
                problem("point_source_mismatch", node->device_id, point.point_id);
            if (!point.enabled && point.disabled_reason.empty())
                problem("disabled_reason_required", node->device_id, point.point_id);
            if (!point.enabled) continue;

            const bool write = kind == "yk" || kind == "yt";
            if ((write && !route->can_control) || (!write && !route->can_collect))
                problem("binding_role_mismatch", node->device_id, point.point_id);
            if (point.register_count < 1 || point.register_count > 4 || point.address_offset < 0 ||
                point.address_offset > 65535 ||
                point.address_offset + point.register_count > 65536 || point.bit_offset < 0 ||
                point.bit_offset > 63)
                problem("address_bounds", node->device_id, point.point_id);
            if (point.byte_order != "" && point.byte_order != "ab" && point.byte_order != "ba")
                problem("wire_layout_unsupported", node->device_id, point.point_id);

            auto channel = channels.find(route->channel_id);
            std::string group;
            using P = std::decay_t<decltype(point)>;
            if constexpr (std::is_same_v<P, CfgPointYkRow> || std::is_same_v<P, CfgPointYtRow>)
                group = point.write_group_key;
            if (channel != channels.end() && !point.address_space.empty())
                addresses.push_back(
                    {route->channel_id + "\n" + route->device_address + "\n" + point.address_space,
                     node->device_id, point.point_id, group, point.address_offset,
                     point.address_offset + point.register_count, point.bit_offset, write});
        }
    };
    visit(records.points_yc, "yc");
    visit(records.points_yx, "yx");
    visit(records.points_yk, "yk");
    visit(records.points_yt, "yt");

    // 能力完整性只阻断当前启用设备；停用设备可按真实配置逐步接入。
    for (const auto& pair : nodes) {
        const auto& node = *pair.second;
        if (!node.enabled || (node.node_kind != "device" && node.node_kind != "component"))
            continue;
        for (const auto& type : records.type_points) {
            if (type.required && type.type_code == node.device_type_code &&
                type.type_version == node.type_version &&
                !semantic_keys.count(node.device_id + "\n" + type.semantic_code))
                problem("required_point_unconfigured", node.device_id);
        }
    }
    for (const auto& point : records.points_yc)
        if (!std::isfinite(point.scale_factor) || point.scale_factor == 0 ||
            point.range_max < point.range_min)
            problem("invalid_yc_conversion", point.owner_device_id, point.point_id);
    for (const auto& point : records.points_yt)
        if (!std::isfinite(point.scale_factor) || point.scale_factor == 0 ||
            point.range_max < point.range_min)
            problem("invalid_yt_conversion", point.owner_device_id, point.point_id);
    for (const auto& point : records.points_yx)
        if (!point.feedback_of_point_id.empty()) {
            auto target = point_owners.find(point.feedback_of_point_id);
            if (target == point_owners.end() || target->second.first != point.owner_device_id ||
                (target->second.second != "yk" && target->second.second != "yt"))
                problem("invalid_feedback_reference", point.owner_device_id, point.point_id);
        }

    auto feedback = [&](const auto& points) {
        for (const auto& point : points) {
            if (point.feedback_json.empty()) continue;
            try {
                const auto rules = nlohmann::json::parse(point.feedback_json);
                if (!rules.is_array() || rules.size() > 32) throw std::invalid_argument("feedback");
                for (const auto& rule : rules) {
                    const auto code = rule.at("point_code").template get<std::string>();
                    bool found = false;
                    for (const auto& value : records.points_yc)
                        if (value.owner_device_id == point.owner_device_id && value.code == code)
                            found = true;
                    for (const auto& value : records.points_yx)
                        if (value.owner_device_id == point.owner_device_id && value.code == code)
                            found = true;
                    if (!found)
                        problem("feedback_point_not_found", point.owner_device_id, point.point_id);
                    const auto criterion = rule.at("criterion").template get<std::string>();
                    if (criterion != "equals" && criterion != "within_tolerance")
                        problem("feedback_criterion_unsupported", point.owner_device_id,
                                point.point_id);
                    if (criterion == "within_tolerance" && rule.value("tolerance", -1.0) < 0)
                        problem("invalid_feedback_tolerance", point.owner_device_id,
                                point.point_id);
                }
            } catch (...) {
                problem("invalid_feedback", point.owner_device_id, point.point_id);
            }
        }
    };
    feedback(records.points_yk);
    feedback(records.points_yt);

    std::sort(addresses.begin(), addresses.end(), [](const auto& a, const auto& b) {
        return std::tie(a.physical, a.begin) < std::tie(b.physical, b.begin);
    });
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        for (std::size_t j = i + 1;
             j < addresses.size() && addresses[j].physical == addresses[i].physical &&
             addresses[j].begin < addresses[i].end;
             ++j) {
            const auto& a = addresses[i];
            const auto& b = addresses[j];
            if (!a.write && !b.write) continue;
            // 读写共享只能由当前地址对应的逐行导入确认解除，不能接受通用忽略开关。
            if (a.write != b.write &&
                (std::find(confirmed_import_points.begin(), confirmed_import_points.end(),
                           a.point) != confirmed_import_points.end() ||
                 std::find(confirmed_import_points.begin(), confirmed_import_points.end(),
                           b.point) != confirmed_import_points.end()))
                continue;
            // 运行写组互斥尚未接入新工程表，当前不提前开放共享写。
            problem(a.write && b.write ? "address_write_conflict"
                                       : "address_overlap_confirmation_required",
                    b.device, b.point);
        }
    }
    return issues;
}
}  // namespace ems::database
