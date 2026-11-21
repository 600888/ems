#include <common/database/configuration/i_device_config_repository.h>
#include <rtdb/model/configuration_catalog.h>

#include <map>
#include <set>

namespace ems::rtdb {
Result<std::shared_ptr<const RuntimeCatalog>> configuration_catalog(
    const database::DeviceConfigRecords& records) {
    auto invalid = [](const std::string& reason) -> Result<std::shared_ptr<const RuntimeCatalog>> {
        return Error{ErrorCode::SchemaMismatch, WriteEffect::NotApplicable, reason};
    };
    DeviceTreeSnapshot tree;
    const database::CfgDeviceRow* root = nullptr;
    std::map<std::string, const database::CfgDeviceRow*> nodes;
    for (const auto& row : records.devices) {
        if (!nodes.emplace(row.device_id, &row).second) return invalid("duplicate device ID");
        if (row.node_kind == "station") {
            if (root) return invalid("unique station required");
            root = &row;
        }
    }
    if (!root || root->config_revision < 0 || root->default_stale_after_ms < 0)
        return invalid("station configuration required");
    auto enabled = [&](const std::string& id) {
        std::set<std::string> visited;
        auto current = id;
        while (!current.empty()) {
            const auto found = nodes.find(current);
            if (found == nodes.end() || !visited.insert(current).second) return false;
            if (!found->second->enabled) return false;
            current = found->second->parent_device_id;
        }
        return true;
    };
    for (const auto& row : records.devices) {
        NodeKind kind;
        if (row.node_kind == "station")
            kind = NodeKind::Station;
        else if (row.node_kind == "unit")
            kind = NodeKind::Unit;
        else if (row.node_kind == "device")
            kind = NodeKind::Device;
        else if (row.node_kind == "component")
            kind = NodeKind::Component;
        else
            return invalid("unknown device node kind");
        tree.nodes.push_back({core::DeviceId{row.device_id}, row.parent_device_id, kind, row.code,
                              enabled(row.device_id)});
    }
    std::map<std::string, bool> channels, bindings;
    std::map<std::string, std::string> control_devices;
    for (const auto& row : records.channels) {
        tree.channels.push_back({core::ChannelId{row.channel_id}, row.code, row.enabled != 0});
        channels[row.channel_id] = row.enabled != 0;
    }
    for (const auto& row : records.bindings) {
        bindings[row.binding_id] =
            row.enabled && enabled(row.device_id) && channels[row.channel_id];
        if (row.can_control) control_devices[row.binding_id] = row.device_id;
    }
    bool valid = true;
    auto append = [&](const auto& rows, model::PointType type) {
        for (const auto& row : rows) {
            model::ValueType value;
            if (row.value_type == "bool")
                value = model::ValueType::Bool;
            else if (row.value_type == "int64")
                value = model::ValueType::Int64;
            else if (row.value_type == "uint64")
                value = model::ValueType::UInt64;
            else if (row.value_type == "string")
                value = model::ValueType::String;
            else if (row.value_type == "double" || row.value_type.empty())
                value = type == model::PointType::YX || type == model::PointType::YK
                            ? model::ValueType::Bool
                            : model::ValueType::Double;
            else {
                valid = false;
                continue;
            }
            // 明确配置的 double 保持为 double，不能因四遥类别改变数值类型。
            if (row.value_type == "double") value = model::ValueType::Double;
            tree.points.push_back(
                {core::PointId{row.point_id}, row.owner_device_id, row.code, type,
                 model::to_point_kind(type), value, row.unit,
                 static_cast<std::uint32_t>(row.stale_after_ms > 0 ? row.stale_after_ms : 0),
                 row.enabled && enabled(row.owner_device_id) && bindings[row.binding_id],
                 model::is_command(type) ? control_devices[row.binding_id] : std::string{}});
        }
    };
    append(records.points_yc, model::PointType::YC);
    append(records.points_yx, model::PointType::YX);
    append(records.points_yk, model::PointType::YK);
    append(records.points_yt, model::PointType::YT);
    if (!valid) return invalid("unsupported point value type");
    RuntimeCatalogOptions options;
    options.scope = {ScopeKind::Station, root->code};
    options.config_version = std::to_string(root->config_revision);
    options.default_stale_after_ms = static_cast<std::uint32_t>(root->default_stale_after_ms);
    options.default_source = {"ems-collector", core::SourceEpoch{1}, options.config_version, true};
    auto built = buildRuntimeCatalog(tree, options);
    if (!built.ok()) return invalid(built.issues.front());
    return std::shared_ptr<const RuntimeCatalog>(
        std::make_shared<RuntimeCatalog>(std::move(built.catalog)));
}
}  // namespace ems::rtdb
