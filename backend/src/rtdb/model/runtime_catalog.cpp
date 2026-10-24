/**
 * @file runtime_catalog.cpp
 * @brief 由设备树快照编译实时运行目录。
 *
 * 只做内存转换：设备树路径、点地址、设备/通道状态键与来源授权。
 * 不访问数据库、不解析点表文件，也不推断点种类。
 */
#include <rtdb/model/runtime_catalog.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ems::rtdb {
namespace {

constexpr std::size_t kMaxPathDepth = 8;    ///< 与 RecordKey 校验一致
constexpr std::size_t kMaxCodeLength = 64;  ///< 与 RecordKey 校验一致

/**
 * @brief 校验节点/点/通道编码，规则与实时库 RecordKey 校验保持一致。
 * @param[in] text 待校验编码。
 * @param[in] device_segment 是否为设备路径段（保留字限制）。
 * @return true 表示可用作记录键片段。
 */
bool validCode(std::string_view text, bool device_segment) {
    if (text.empty() || text.size() > kMaxCodeLength) return false;
    const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(text.front())) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (!alnum(c) && c != '_' && c != '-') return false;
        if (c == '-' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9') {
            return false;
        }
    }
    if (device_segment) {
        for (const char* reserved :
             {"meta", "rt", "config", "stream", "command", "channel", "resource", "process"}) {
            if (text == reserved) return false;
        }
    }
    return true;
}

std::string addressKey(const PointAddress& address) {
    std::string key = address.scope.kind == ScopeKind::Station ? "station|" : "unit|";
    key += address.scope.code;
    key += '|';
    for (const auto& part : address.device_path) {
        key += part;
        key += '/';
    }
    key += address.code;
    return key;
}

std::string capabilityKey(const core::DeviceId& device_id, const std::string& code) {
    return device_id.value() + "|" + code;
}

/**
 * @brief 判断节点种类是否为作用域根种类。
 * @param[in] kind 节点种类。
 * @param[in] scope_kind 作用域种类。
 * @return true 表示该节点种类可作为对应作用域的根。
 */
bool rootKindMatches(NodeKind kind, ScopeKind scope_kind) {
    return scope_kind == ScopeKind::Station ? kind == NodeKind::Station : kind == NodeKind::Unit;
}

/// 路径解析上下文：索引设备树并缓存节点到作用域根的相对路径。
class PathResolver {
   public:
    /**
     * @brief 建立节点索引并报告结构性问题。
     * @param[in] snapshot 设备树快照。
     * @param[in,out] issues 构建问题列表。
     */
    explicit PathResolver(const DeviceTreeSnapshot& snapshot, std::vector<std::string>& issues) {
        for (const auto& node : snapshot.nodes) {
            if (node.node_id.empty()) {
                issues.emplace_back("设备树节点 ID 为空");
                continue;
            }
            if (node.parent_node_id == node.node_id.value()) {
                issues.emplace_back("设备树节点父指针指向自身: " + node.node_id.value());
                continue;
            }
            if (!index_.emplace(node.node_id.value(), &node).second) {
                issues.emplace_back("设备树节点 ID 重复: " + node.node_id.value());
            }
        }
    }

    /**
     * @brief 解析节点相对作用域根的路径。
     * @param[in] node 目标节点。
     * @param[in] root 作用域根节点。
     * @param[out] path 解析结果；根节点为空路径。
     * @param[out] reason 失败原因。
     * @return true 表示解析成功（节点在作用域根之下）。
     */
    bool resolve(const CatalogNode& node, const CatalogNode& root, std::vector<std::string>& path,
                 std::string& reason) {
        if (node.node_id == root.node_id) {
            path.clear();
            return true;
        }
        const auto cached = cache_.find(node.node_id.value());
        if (cached != cache_.end()) {
            path = cached->second;
            return true;
        }

        std::vector<const CatalogNode*> chain;
        std::unordered_set<std::string> visited;
        const CatalogNode* current = &node;
        while (current->node_id != root.node_id) {
            if (!visited.insert(current->node_id.value()).second) {
                reason = "设备树成环，无法解析路径: " + node.node_id.value();
                return false;
            }
            chain.push_back(current);
            if (current->parent_node_id.empty()) {
                reason = "节点不在作用域根之下: " + node.node_id.value();
                return false;
            }
            current = lookup(current->parent_node_id);
            if (current == nullptr) {
                reason = "父节点不存在: " + node.node_id.value();
                return false;
            }
        }

        std::vector<std::string> resolved;
        resolved.reserve(chain.size());
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (!validCode((*it)->code, true)) {
                reason = "设备路径段编码非法: " + (*it)->code;
                return false;
            }
            resolved.push_back((*it)->code);
        }
        if (resolved.size() > kMaxPathDepth) {
            reason = "设备路径超过 8 级: " + node.node_id.value();
            return false;
        }
        cache_.emplace(node.node_id.value(), resolved);
        path = std::move(resolved);
        return true;
    }

    /**
     * @brief 按稳定节点 ID 查找节点。
     * @param[in] id 节点 ID 文本。
     * @return 匹配的节点；未找到时为空。
     */
    const CatalogNode* lookup(const std::string& id) const {
        const auto it = index_.find(id);
        return it == index_.end() ? nullptr : it->second;
    }

   private:
    std::unordered_map<std::string, const CatalogNode*> index_;
    std::unordered_map<std::string, std::vector<std::string>> cache_;
};

}  // namespace

RuntimeCatalog::RuntimeCatalog(Scope scope, std::string config_version,
                               std::string activation_revision,
                               std::uint32_t default_stale_after_ms)
    : scope_(std::move(scope)),
      config_version_(std::move(config_version)),
      activation_revision_(std::move(activation_revision)),
      default_stale_after_ms_(default_stale_after_ms == 0 ? 60000 : default_stale_after_ms) {}

bool RuntimeCatalog::addPoint(RuntimePoint point) {
    if (point.point_id.empty() || !validCode(point.address.code, false)) return false;
    const std::string key = addressKey(point.address);
    if (point_address_index_.count(key) != 0) return false;
    if (point_id_index_.count(point.point_id.value()) != 0) return false;
    point_address_index_.emplace(key, points_.size());
    point_id_index_.emplace(point.point_id.value(), points_.size());
    points_.push_back(std::move(point));
    return true;
}

bool RuntimeCatalog::addDevice(RuntimeDevice device) {
    if (device.device_id.empty() || device.device_path.empty()) return false;
    if (!validCode(device.code, false)) return false;
    if (device_index_.count(device.device_id.value()) != 0) return false;
    device_index_.emplace(device.device_id.value(), devices_.size());
    devices_.push_back(std::move(device));
    return true;
}

bool RuntimeCatalog::addChannel(RuntimeChannel channel) {
    if (channel.channel_id.empty() || !validCode(channel.code, false)) return false;
    if (channel_index_.count(channel.channel_id.value()) != 0) return false;
    channel_index_.emplace(channel.channel_id.value(), channels_.size());
    channels_.push_back(std::move(channel));
    return true;
}

bool RuntimeCatalog::addCapability(RuntimeCapability capability) {
    if (capability.device_id.empty() || capability.capability_code.empty()) return false;
    const std::string key = capabilityKey(capability.device_id, capability.capability_code);
    if (capability_index_.count(key) != 0) return false;
    capability_index_.emplace(key, capabilities_.size());
    capabilities_.push_back(std::move(capability));
    return true;
}

const RuntimePoint* RuntimeCatalog::findPoint(const PointAddress& address) const {
    const auto it = point_address_index_.find(addressKey(address));
    return it == point_address_index_.end() ? nullptr : &points_[it->second];
}

const RuntimePoint* RuntimeCatalog::findPoint(const core::PointId& point_id) const {
    const auto it = point_id_index_.find(point_id.value());
    return it == point_id_index_.end() ? nullptr : &points_[it->second];
}

const RuntimeDevice* RuntimeCatalog::findDevice(const core::DeviceId& device_id) const {
    const auto it = device_index_.find(device_id.value());
    return it == device_index_.end() ? nullptr : &devices_[it->second];
}

const RuntimeChannel* RuntimeCatalog::findChannel(const core::ChannelId& channel_id) const {
    const auto it = channel_index_.find(channel_id.value());
    return it == channel_index_.end() ? nullptr : &channels_[it->second];
}

const RuntimeCapability* RuntimeCatalog::findCapability(const core::DeviceId& device_id,
                                                        const std::string& capability_code) const {
    const auto it = capability_index_.find(capabilityKey(device_id, capability_code));
    return it == capability_index_.end() ? nullptr : &capabilities_[it->second];
}

RuntimeCatalogBuildResult buildRuntimeCatalog(const DeviceTreeSnapshot& snapshot,
                                              const RuntimeCatalogOptions& options) {
    RuntimeCatalogBuildResult result;
    result.options = options;

    auto& issues = result.issues;
    if (!validCode(options.scope.code, false)) {
        issues.emplace_back("作用域编码非法或为空: " + options.scope.code);
    }
    if (options.config_version.empty()) {
        issues.emplace_back("配置版本为空");
    }
    if (issues.empty()) {
        result.catalog =
            RuntimeCatalog(options.scope, options.config_version, options.activation_revision,
                           options.default_stale_after_ms);
    }
    if (!issues.empty()) return result;

    PathResolver resolver(snapshot, issues);

    // 作用域根：按 node_kind(Station/Unit) + code 唯一匹配。
    const CatalogNode* root = nullptr;
    for (const auto& node : snapshot.nodes) {
        if (!rootKindMatches(node.kind, options.scope.kind) || node.code != options.scope.code) {
            continue;
        }
        if (root != nullptr) {
            issues.emplace_back("作用域根节点不唯一: " + options.scope.code);
            return result;
        }
        root = &node;
    }
    if (root == nullptr) {
        issues.emplace_back("找不到作用域根节点: " + options.scope.code);
        return result;
    }

    // 站根：通道状态记录固定使用站作用域。
    const CatalogNode* station = root;
    while (station->kind != NodeKind::Station) {
        if (station->parent_node_id.empty()) {
            issues.emplace_back("作用域根不在任何站根之下: " + options.scope.code);
            return result;
        }
        station = resolver.lookup(station->parent_node_id);
        if (station == nullptr) {
            issues.emplace_back("父节点不存在: " + options.scope.code);
            return result;
        }
    }
    const std::string station_code = station->code;

    std::vector<std::string> path;
    std::string reason;

    for (const auto& node : snapshot.nodes) {
        // 设备与部件产生状态记录；站根/单元根是作用域运行记录，不单独建设备记录。
        if (node.kind != NodeKind::Device && node.kind != NodeKind::Component) continue;
        if (!resolver.resolve(node, *root, path, reason)) {
            // 作用域根之外的节点（其他单元/其他站）不属于本目录，跳过；结构性问题仍报出。
            if (reason.find("不在作用域根之下") == std::string::npos) {
                issues.emplace_back("设备 " + node.node_id.value() + ": " + reason);
            }
            continue;
        }
        if (path.empty()) {
            issues.emplace_back("设备 " + node.node_id.value() +
                                ": 设备节点不能同时是作用域根，状态记录会与作用域记录冲突");
            continue;
        }
        RuntimeDevice runtime;
        runtime.device_id = node.node_id;
        runtime.code = node.code;
        runtime.scope = options.scope;
        runtime.device_path = path;
        runtime.enabled = node.enabled;
        if (!result.catalog.addDevice(std::move(runtime))) {
            issues.emplace_back("设备定义重复或编码非法: " + node.node_id.value());
        }
    }

    for (const auto& point : snapshot.points) {
        const CatalogNode* owner = resolver.lookup(point.owner_node_id);
        if (owner == nullptr) {
            issues.emplace_back("点 " + point.point_id.value() +
                                ": 归属节点不存在: " + point.owner_node_id);
            continue;
        }
        if (!model::point_type_matches_kind(point.point_type, point.kind)) {
            issues.emplace_back("点 " + point.point_id.value() + ": point_type 与 kind 不自洽");
            continue;
        }
        if (!resolver.resolve(*owner, *root, path, reason)) {
            if (reason.find("不在作用域根之下") == std::string::npos) {
                issues.emplace_back("点 " + point.point_id.value() + ": " + reason);
            }
            continue;
        }
        RuntimePoint runtime;
        runtime.point_id = point.point_id;
        // 设备与部件节点都有稳定 ID；单元/站根点不挂设备。
        if (owner->kind == NodeKind::Device || owner->kind == NodeKind::Component) {
            runtime.device_id = owner->node_id;
        }
        runtime.address.scope = options.scope;
        runtime.address.device_path = path;
        runtime.address.code = point.code;
        runtime.point_type = point.point_type;
        runtime.kind = point.kind;
        runtime.value_type = point.value_type;
        runtime.unit = point.unit;
        runtime.stale_after_ms =
            point.stale_after_ms != 0
                ? point.stale_after_ms
                : (options.default_stale_after_ms == 0 ? 60000 : options.default_stale_after_ms);
        runtime.enabled = point.enabled;
        runtime.source = options.default_source;
        if (runtime.source.config_version.empty()) {
            runtime.source.config_version = options.config_version;
        }
        if (!result.catalog.addPoint(std::move(runtime))) {
            issues.emplace_back("点地址或点 ID 重复: " + point.point_id.value());
        }
    }

    for (const auto& channel : snapshot.channels) {
        RuntimeChannel runtime;
        runtime.channel_id = channel.channel_id;
        runtime.code = channel.code;
        runtime.scope = Scope{ScopeKind::Station, station_code};
        runtime.enabled = channel.enabled;
        if (!result.catalog.addChannel(std::move(runtime))) {
            issues.emplace_back("通道定义重复或编码非法: " + channel.channel_id.value());
        }
    }

    result.point_count = result.catalog.pointCount();
    // 能力依赖由配置侧按设备草稿装配，不能从类型目录猜测设备实例。
    return result;
}

}  // namespace ems::rtdb
