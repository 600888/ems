/**
 * @file runtime_catalog.cpp
 * @brief 由公共模型目录编译实时运行目录。
 *
 * 只做内存转换：资产路径、点地址、设备/通道状态键与来源授权。
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
 * @brief 校验资产/点/通道编码，规则与实时库 RecordKey 校验保持一致。
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

/// 路径解析上下文：缓存资产到作用域根的相对路径。
class PathResolver {
   public:
    PathResolver(const model::ModelCatalog& catalog, std::string root_id)
        : root_id_(std::move(root_id)) {
        for (const auto& asset : catalog.assets)
            asset_index_.emplace(asset.asset_id.value(), &asset);
    }

    /**
     * @brief 解析资产相对作用域根的路径。
     * @param[in] asset_id 资产 ID。
     * @param[out] path 解析结果；根节点为空路径。
     * @param[out] reason 失败原因。
     * @return true 表示解析成功。
     */
    bool resolve(const core::AssetId& asset_id, std::vector<std::string>& path,
                 std::string& reason) {
        if (asset_id.empty()) {
            reason = "资产 ID 为空";
            return false;
        }
        if (asset_id.value() == root_id_) {
            path.clear();
            return true;
        }
        const auto cached = cache_.find(asset_id.value());
        if (cached != cache_.end()) {
            path = cached->second;
            return true;
        }

        std::vector<const model::AssetNode*> chain;
        std::unordered_set<std::string> visited;
        const model::AssetNode* current = lookup(asset_id.value());
        while (current != nullptr && current->asset_id.value() != root_id_) {
            if (!visited.insert(current->asset_id.value()).second) {
                reason = "资产树成环，无法解析路径: " + asset_id.value();
                return false;
            }
            chain.push_back(current);
            if (!current->parent_asset_id.has_value()) {
                reason = "资产不在作用域根之下: " + asset_id.value();
                return false;
            }
            current = lookup(current->parent_asset_id->value());
            if (current == nullptr) {
                reason = "父资产不存在: " + asset_id.value();
                return false;
            }
        }
        if (current == nullptr) {
            reason = "资产不在作用域根之下: " + asset_id.value();
            return false;
        }

        std::vector<std::string> resolved;
        resolved.reserve(chain.size());
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::string& code = (*it)->code;
            if (!validCode(code, true)) {
                reason = "资产路径段编码非法: " + code;
                return false;
            }
            resolved.push_back(code);
        }
        if (resolved.size() > kMaxPathDepth) {
            reason = "资产路径超过 8 级: " + asset_id.value();
            return false;
        }
        cache_.emplace(asset_id.value(), resolved);
        path = std::move(resolved);
        return true;
    }

   private:
    const model::AssetNode* lookup(const std::string& id) const {
        const auto it = asset_index_.find(id);
        return it == asset_index_.end() ? nullptr : it->second;
    }

    std::string root_id_;
    std::unordered_map<std::string, const model::AssetNode*> asset_index_;
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

RuntimeCatalogBuildResult buildRuntimeCatalog(const model::ModelCatalog& catalog,
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

    const model::AssetNode* root = nullptr;
    for (const auto& asset : catalog.assets) {
        const bool kind_matches = (options.scope.kind == ScopeKind::Unit &&
                                   asset.asset_type == model::AssetType::EssUnit) ||
                                  (options.scope.kind == ScopeKind::Station &&
                                   asset.asset_type == model::AssetType::Station);
        if (!kind_matches || asset.code != options.scope.code) continue;
        if (root != nullptr) {
            issues.emplace_back("作用域根资产不唯一: " + options.scope.code);
            return result;
        }
        root = &asset;
    }
    if (root == nullptr) {
        issues.emplace_back("找不到作用域根资产: " + options.scope.code);
        return result;
    }

    const std::string station_code = root->station_id.value();
    std::unordered_map<std::string, core::DeviceId> device_by_asset;
    for (const auto& device : catalog.devices) {
        device_by_asset.emplace(device.asset_id.value(), device.device_id);
    }
    PathResolver paths(catalog, root->asset_id.value());
    std::vector<std::string> path;
    std::string reason;

    for (const auto& device : catalog.devices) {
        if (!paths.resolve(device.asset_id, path, reason)) {
            issues.emplace_back("设备 " + device.device_id.value() + ": " + reason);
            continue;
        }
        if (path.empty()) {
            issues.emplace_back("设备 " + device.device_id.value() +
                                ": 设备资产不能同时是作用域根，状态记录会与作用域记录冲突");
            continue;
        }
        RuntimeDevice runtime;
        runtime.device_id = device.device_id;
        runtime.code = device.code.empty() ? path.back() : device.code;
        runtime.scope = options.scope;
        runtime.device_path = path;
        runtime.enabled = device.enabled;
        if (!result.catalog.addDevice(std::move(runtime))) {
            issues.emplace_back("设备定义重复或编码非法: " + device.device_id.value());
        }
    }

    catalog.for_each_point([&](const model::PointBase& point) {
        if (!paths.resolve(point.asset_id, path, reason)) {
            issues.emplace_back("点 " + point.point_id.value() + ": " + reason);
            return;
        }
        RuntimePoint runtime;
        runtime.point_id = point.point_id;
        const auto owner = device_by_asset.find(point.asset_id.value());
        if (owner != device_by_asset.end()) runtime.device_id = owner->second;
        runtime.address.scope = options.scope;
        runtime.address.device_path = path;
        runtime.address.code = point.code;
        runtime.point_type = point.point_type;
        runtime.kind = point.kind;
        runtime.value_type = point.value_type;
        runtime.unit = point.unit;
        runtime.stale_after_ms =
            options.default_stale_after_ms == 0 ? 60000 : options.default_stale_after_ms;
        runtime.enabled = point.enabled;
        runtime.source = options.default_source;
        if (runtime.source.config_version.empty()) {
            runtime.source.config_version = options.config_version;
        }
        if (!result.catalog.addPoint(std::move(runtime))) {
            issues.emplace_back("点地址或点 ID 重复: " + point.point_id.value());
        }
    });

    for (const auto& channel : catalog.channels) {
        if (channel.station_id.value() != station_code) continue;
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
