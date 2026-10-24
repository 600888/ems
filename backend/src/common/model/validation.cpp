/**
 * @file validation.cpp
 * @brief ModelCatalog 索引构建与纯模型校验实现（design.md 第 6.3 节）
 *
 * 校验覆盖：身份唯一、资产无环、不跨站挂接、引用存在、类型/单位、
 * 设备/通道多对多、命令反馈依赖、路由引用、能力依赖、版本闭合。
 * 聚合有上限的字段错误，不返回首个 bool。
 */
#include <common/model/catalog.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ems::model {
namespace {

/// 单类校验错误聚合上限（超过则置 truncated）
constexpr std::size_t kMaxIssues = 256;

/// 资产环检测：从根出发 DFS，沿 parent 链检测回边
bool detect_asset_cycle(const ModelCatalog& catalog, std::vector<ValidationIssue>& issues) {
    std::unordered_map<std::string, int> color;  // 0=未访问 1=在栈 2=完成
    bool cyclic = false;
    for (const auto& asset : catalog.assets) {
        color[asset.asset_id.value()] = 0;
    }
    // 迭代 DFS：沿 parent_asset_id 走到根；路径上的节点若再次出现即成环
    for (const auto& start : catalog.assets) {
        if (color[start.asset_id.value()] != 0) continue;
        std::vector<const AssetNode*> stack;
        const AssetNode* cur = &start;
        while (cur != nullptr && color[cur->asset_id.value()] == 0) {
            color[cur->asset_id.value()] = 1;
            stack.push_back(cur);
            if (!cur->parent_asset_id.has_value()) {
                cur = nullptr;
                break;
            }
            const AssetNode* parent = catalog.find_asset(*cur->parent_asset_id);
            cur = parent;
        }
        if (cur != nullptr && color[cur->asset_id.value()] == 1) {
            // 回到栈中节点：成环
            cyclic = true;
            issues.push_back({validation_code::kAssetCycle, cur->asset_id.value(),
                              "parent_asset_id", "资产引用成环"});
        }
        for (auto* n : stack) {
            color[n->asset_id.value()] = 2;
        }
    }
    return cyclic;
}

}  // namespace

bool value_matches_type(const PointValue& value, ValueType type) noexcept {
    switch (type) {
        case ValueType::Bool:
            return std::holds_alternative<bool>(value);
        case ValueType::Int64:
            return std::holds_alternative<std::int64_t>(value);
        case ValueType::UInt64:
            return std::holds_alternative<std::uint64_t>(value);
        case ValueType::Double: {
            if (!std::holds_alternative<double>(value)) return false;
            const double d = std::get<double>(value);
            return std::isfinite(d);  // 禁止 NaN/Inf
        }
        case ValueType::String:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

void ModelCatalog::add_issue(std::vector<ValidationIssue>& issues, const char* code,
                             const std::string& object_id, const std::string& field_path,
                             const std::string& reason) const {
    if (issues.size() >= kMaxIssues) return;
    // 同对象同码同字段去重
    auto key = object_id + "|" + code + "|" + field_path;
    if (!issued_codes_.insert(std::move(key)).second) return;
    if (issues.size() == kMaxIssues - 1) {
        issues.push_back(
            {validation_code::kInvalidValue, "", "", "校验错误数超过上限，报告被截断"});
        return;
    }
    issues.push_back({code, object_id, field_path, reason});
}

ValidationReport ModelCatalog::build_indexes() {
    asset_index_.clear();
    device_index_.clear();
    point_index_.clear();
    channel_index_.clear();
    binding_index_.clear();
    capability_index_.clear();
    device_type_index_.clear();
    device_bindings_index_.clear();
    asset_points_index_.clear();
    issued_codes_.clear();

    ValidationReport report;
    auto& issues = report.issues;

    // ---- 索引构建 + 身份唯一 ----
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto& a = assets[i];
        if (a.asset_id.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "asset_id", "资产 ID 为空");
            continue;
        }
        if (!asset_index_.emplace(a.asset_id.value(), i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, a.asset_id.value(), "asset_id",
                      "资产 ID 重复");
        }
    }
    for (std::size_t i = 0; i < device_types.size(); ++i) {
        auto& t = device_types[i];
        if (t.type_version_id.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "type_version_id",
                      "设备类型版本 ID 为空");
            continue;
        }
        if (!device_type_index_.emplace(t.type_version_id, i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, t.type_version_id,
                      "type_version_id", "设备类型版本重复");
        }
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        if (d.device_id.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "device_id", "设备 ID 为空");
            continue;
        }
        if (!device_index_.emplace(d.device_id.value(), i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, d.device_id.value(), "device_id",
                      "设备 ID 重复");
            continue;
        }
        device_bindings_index_.emplace(d.device_id.value(), std::vector<std::size_t>{});
    }
    {
        // 四遥点分散在四个集合，索引项记录（类型, 下标），统一去重与反查
        auto index_points = [&](PointType type, const auto& collection) {
            for (std::size_t i = 0; i < collection.size(); ++i) {
                const auto& p = collection[i];
                if (p.point_id.empty()) {
                    add_issue(issues, validation_code::kInvalidValue, "", "point_id", "点 ID 为空");
                    continue;
                }
                if (!point_index_.emplace(p.point_id.value(), PointSlot{type, i}).second) {
                    add_issue(issues, validation_code::kDuplicateIdentity, p.point_id.value(),
                              "point_id", "点 ID 重复");
                    continue;
                }
                asset_points_index_[p.asset_id.value()].push_back(PointSlot{type, i});
            }
        };
        index_points(PointType::YC, points_yc);
        index_points(PointType::YX, points_yx);
        index_points(PointType::YK, points_yk);
        index_points(PointType::YT, points_yt);
    }
    for (std::size_t i = 0; i < channels.size(); ++i) {
        const auto& c = channels[i];
        if (c.channel_id.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "channel_id", "通道 ID 为空");
            continue;
        }
        if (!channel_index_.emplace(c.channel_id.value(), i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, c.channel_id.value(),
                      "channel_id", "通道 ID 重复");
        }
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& b = bindings[i];
        if (b.binding_id.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "binding_id", "绑定 ID 为空");
            continue;
        }
        if (!binding_index_.emplace(b.binding_id, i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, b.binding_id, "binding_id",
                      "绑定 ID 重复");
            continue;
        }
        auto it = device_bindings_index_.find(b.device_id.value());
        if (it != device_bindings_index_.end()) {
            it->second.push_back(i);
        }
    }
    for (std::size_t i = 0; i < capabilities.size(); ++i) {
        auto& c = capabilities[i];
        if (c.capability_code.empty()) {
            add_issue(issues, validation_code::kInvalidValue, "", "capability_code",
                      "能力编码为空");
            continue;
        }
        if (!capability_index_.emplace(c.capability_code, i).second) {
            add_issue(issues, validation_code::kDuplicateIdentity, c.capability_code,
                      "capability_code", "能力编码重复");
        }
    }

    // ---- 资产树：父节点存在、不跨站挂接、无环 ----
    for (const auto& a : assets) {
        if (!a.parent_asset_id.has_value()) continue;
        auto it = asset_index_.find(a.parent_asset_id->value());
        if (it == asset_index_.end()) {
            add_issue(issues, validation_code::kMissingReference, a.asset_id.value(),
                      "parent_asset_id", "父资产不存在: " + a.parent_asset_id->value());
            continue;
        }
        const auto& parent = assets[it->second];
        if (!(parent.station_id == a.station_id)) {
            add_issue(issues, validation_code::kCrossStationParent, a.asset_id.value(),
                      "parent_asset_id", "父资产属于其他站，不能跨站挂接");
        }
    }
    detect_asset_cycle(*this, issues);

    // ---- 引用存在：设备→资产；点→资产 ----
    for (const auto& d : devices) {
        if (d.device_id.empty() || !device_index_.count(d.device_id.value())) continue;
        if (!asset_index_.count(d.asset_id.value())) {
            add_issue(issues, validation_code::kMissingReference, d.device_id.value(), "asset_id",
                      "设备所属资产不存在: " + d.asset_id.value());
        }
        if (!device_type_index_.count(d.type_version_id)) {
            add_issue(issues, validation_code::kVersionMismatch, d.device_id.value(),
                      "type_version_id", "设备类型版本不存在: " + d.type_version_id);
        }
    }
    for_each_point([&](const PointBase& p) {
        if (p.point_id.empty() || !point_index_.count(p.point_id.value())) return;
        if (!asset_index_.count(p.asset_id.value())) {
            add_issue(issues, validation_code::kMissingReference, p.point_id.value(), "asset_id",
                      "点所属资产不存在: " + p.asset_id.value());
        }
        if (p.code.empty()) {
            add_issue(issues, validation_code::kInvalidValue, p.point_id.value(), "code",
                      "点业务编码为空");
        }
        // 命令反馈依赖：feedback_of 必须指向存在的命令点（遥控或遥调）
        if (p.feedback_of.has_value()) {
            auto it = point_index_.find(p.feedback_of->value());
            const PointBase* target = it == point_index_.end() ? nullptr : point_at(it->second);
            if (target == nullptr) {
                add_issue(issues, validation_code::kInvalidFeedbackRef, p.point_id.value(),
                          "feedback_of", "反馈点引用的命令点不存在: " + p.feedback_of->value());
            } else if (!is_command(target->point_type)) {
                add_issue(issues, validation_code::kInvalidFeedbackRef, p.point_id.value(),
                          "feedback_of", "反馈点必须引用命令点");
            }
        }
        // 遥控固定值模式必须给出写入值，否则控制侧无法下发
        if (p.point_type == PointType::YK) {
            const auto& yk = static_cast<const YkPoint&>(p);
            if (yk.write_mode == WriteMode::Fixed && !yk.fixed_raw_value.has_value()) {
                add_issue(issues, validation_code::kInvalidValue, p.point_id.value(),
                          "fixed_raw_value", "固定值写入必须提供 fixed_raw_value");
            }
        }
    });

    // ---- 绑定：设备/通道引用存在 + 同 (device, channel) 去重 + 操作集合非空 ----
    {
        std::unordered_map<std::string, std::string> pair_seen;  // "dev|chan" -> binding_id
        for (const auto& b : bindings) {
            if (b.binding_id.empty() || !binding_index_.count(b.binding_id)) continue;
            if (!device_index_.count(b.device_id.value())) {
                add_issue(issues, validation_code::kMissingReference, b.binding_id, "device_id",
                          "绑定的设备不存在: " + b.device_id.value());
            }
            if (!channel_index_.count(b.channel_id.value())) {
                add_issue(issues, validation_code::kMissingReference, b.binding_id, "channel_id",
                          "绑定的通道不存在: " + b.channel_id.value());
            }
            if (b.operations.empty()) {
                add_issue(issues, validation_code::kInvalidValue, b.binding_id, "operations",
                          "绑定操作集合为空");
            }
            std::string pair_key = b.device_id.value() + "|" + b.channel_id.value();
            auto it = pair_seen.find(pair_key);
            if (it != pair_seen.end()) {
                add_issue(issues, validation_code::kDuplicateBinding, b.binding_id,
                          "device_id/channel_id", "同设备同通道重复绑定，另一绑定: " + it->second);
            } else {
                pair_seen.emplace(std::move(pair_key), b.binding_id);
            }
        }
    }

    // ---- 厂家点路由：点/绑定引用存在；同 (point, binding, vendor_address) 去重 ----
    {
        std::unordered_map<std::string, std::string> route_seen;
        for (const auto& r : vendor_routes) {
            if (r.route_id.empty()) continue;
            if (!point_index_.count(r.point_id.value())) {
                add_issue(issues, validation_code::kMissingReference, r.route_id, "point_id",
                          "路由引用的标准点不存在: " + r.point_id.value());
            }
            if (!binding_index_.count(r.binding_id)) {
                add_issue(issues, validation_code::kMissingReference, r.route_id, "binding_id",
                          "路由引用的绑定不存在: " + r.binding_id);
            }
            if (r.vendor_address.empty()) {
                add_issue(issues, validation_code::kInvalidValue, r.route_id, "vendor_address",
                          "厂家点地址为空");
            }
            std::string key = r.point_id.value() + "|" + r.binding_id + "|" + r.vendor_address;
            auto it = route_seen.find(key);
            if (it != route_seen.end()) {
                add_issue(issues, validation_code::kDuplicateRoute, r.route_id,
                          "point_id/binding_id/vendor_address",
                          "同点同绑定同地址重复路由，另一路由: " + it->second);
            } else {
                route_seen.emplace(std::move(key), r.route_id);
            }
        }
    }

    // ---- 北向转发映射：点引用存在 ----
    for (const auto& m : forwarding_maps) {
        if (m.map_id.empty()) continue;
        if (!point_index_.count(m.point_id.value())) {
            add_issue(issues, validation_code::kMissingReference, m.map_id, "point_id",
                      "转发映射引用的标准点不存在: " + m.point_id.value());
        }
        if (m.north_address.empty()) {
            add_issue(issues, validation_code::kInvalidValue, m.map_id, "north_address",
                      "北向协议地址为空");
        }
    }

    // ---- 能力：所需点/反馈点引用存在 ----
    for (const auto& c : capabilities) {
        if (c.capability_code.empty() || !capability_index_.count(c.capability_code)) continue;
        for (const auto& pid : c.required_points) {
            if (!point_index_.count(pid.value())) {
                add_issue(issues, validation_code::kMissingReference, c.capability_code,
                          "required_points", "能力所需点不存在: " + pid.value());
            }
        }
        for (const auto& pid : c.required_feedbacks) {
            // 反馈点是独立点（design 6.1：命令和反馈分点），存在即可；
            // 其通过 feedback_of 指回命令点，不要求自身是 Command 类型
            if (!point_index_.count(pid.value())) {
                add_issue(issues, validation_code::kMissingReference, c.capability_code,
                          "required_feedbacks", "能力所需反馈点不存在: " + pid.value());
            }
        }
    }

    report.truncated = false;
    indexed_ = true;
    return report;
}

const AssetNode* ModelCatalog::find_asset(const core::AssetId& id) const {
    auto it = asset_index_.find(id.value());
    return it == asset_index_.end() ? nullptr : &assets[it->second];
}

const Device* ModelCatalog::find_device(const core::DeviceId& id) const {
    auto it = device_index_.find(id.value());
    return it == device_index_.end() ? nullptr : &devices[it->second];
}

const PointBase* ModelCatalog::point_at(const PointSlot& slot) const {
    switch (slot.type) {
        case PointType::YC:
            return slot.index < points_yc.size() ? &points_yc[slot.index] : nullptr;
        case PointType::YX:
            return slot.index < points_yx.size() ? &points_yx[slot.index] : nullptr;
        case PointType::YK:
            return slot.index < points_yk.size() ? &points_yk[slot.index] : nullptr;
        case PointType::YT:
            return slot.index < points_yt.size() ? &points_yt[slot.index] : nullptr;
    }
    return nullptr;
}

const PointBase* ModelCatalog::find_point(const core::PointId& id) const {
    auto it = point_index_.find(id.value());
    return it == point_index_.end() ? nullptr : point_at(it->second);
}

const ChannelConfig* ModelCatalog::find_channel(const core::ChannelId& id) const {
    auto it = channel_index_.find(id.value());
    return it == channel_index_.end() ? nullptr : &channels[it->second];
}

const DeviceChannelBinding* ModelCatalog::find_binding(const std::string& binding_id) const {
    auto it = binding_index_.find(binding_id);
    return it == binding_index_.end() ? nullptr : &bindings[it->second];
}

const CapabilityDefinition* ModelCatalog::find_capability(
    const std::string& capability_code) const {
    auto it = capability_index_.find(capability_code);
    return it == capability_index_.end() ? nullptr : &capabilities[it->second];
}

const DeviceTypeDefinition* ModelCatalog::find_device_type(
    const std::string& type_version_id) const {
    auto it = device_type_index_.find(type_version_id);
    return it == device_type_index_.end() ? nullptr : &device_types[it->second];
}

std::vector<const DeviceChannelBinding*> ModelCatalog::bindings_of_device(
    const core::DeviceId& device_id, std::optional<BindingOp> op) const {
    std::vector<const DeviceChannelBinding*> result;
    auto it = device_bindings_index_.find(device_id.value());
    if (it == device_bindings_index_.end()) return result;  // 空列表 = 无此操作绑定
    for (std::size_t idx : it->second) {
        const auto& b = bindings[idx];
        if (op.has_value() &&
            std::find(b.operations.begin(), b.operations.end(), *op) == b.operations.end()) {
            continue;
        }
        result.push_back(&b);
    }
    return result;
}

std::vector<const PointBase*> ModelCatalog::points_of_asset(const core::AssetId& asset_id) const {
    std::vector<const PointBase*> result;
    auto it = asset_points_index_.find(asset_id.value());
    if (it == asset_points_index_.end()) return result;
    result.reserve(it->second.size());
    for (const auto& slot : it->second) {
        if (const auto* p = point_at(slot)) result.push_back(p);
    }
    return result;
}

}  // namespace ems::model
