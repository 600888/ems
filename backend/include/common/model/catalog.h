/**
 * @file catalog.h
 * @brief 模型聚合目录：构建时建立索引的只读集合（design.md 第 6.2/6.3 节）
 *
 * 公共模型采用值对象、稳定 ID 引用和只读集合，不用双向 shared_ptr
 * 拼出可变对象图。聚合配置在构建时建立索引；发布后通过
 * shared_ptr<const ConfigSnapshot> 共享，读取者持有旧版本期间旧对象保持有效。
 */
#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <common/core/identity.h>
#include <common/model/asset.h>
#include <common/model/binding.h>
#include <common/model/capability.h>
#include <common/model/channel.h>
#include <common/model/device.h>
#include <common/model/point.h>
#include <common/model/validation.h>

namespace ems::model {

/// 一份完整候选/生效配置的模型聚合
class ModelCatalog {
public:
    // ---- 数据集合（发布前可填充；build_indexes 后视作只读）----
    std::vector<AssetNode> assets;
    std::vector<EssUnitInfo> ess_units;
    std::vector<DeviceTypeDefinition> device_types;
    std::vector<Device> devices;
    std::vector<Point> points;
    std::vector<ChannelConfig> channels;
    std::vector<DeviceChannelBinding> bindings;
    std::vector<CapabilityDefinition> capabilities;
    std::vector<VendorPointRoute> vendor_routes;
    std::vector<ForwardingPointMap> forwarding_maps;

    /**
     * @brief 构建索引并执行纯模型校验（design.md 6.3 validate_snapshot）
     * @return 聚合校验报告（身份唯一、资产无环、引用存在、类型/单位、
     *         多对多绑定、能力依赖、路由引用、版本闭合等）
     */
    ValidationReport build_indexes();

    bool indexed() const noexcept { return indexed_; }

    // ---- 只读查找（内存操作，不回源数据库）----
    const AssetNode* find_asset(const core::AssetId& id) const;
    const Device* find_device(const core::DeviceId& id) const;
    const Point* find_point(const core::PointId& id) const;
    const ChannelConfig* find_channel(const core::ChannelId& id) const;
    const DeviceChannelBinding* find_binding(const std::string& binding_id) const;
    const CapabilityDefinition* find_capability(const std::string& capability_code) const;
    const DeviceTypeDefinition* find_device_type(const std::string& type_version_id) const;

    /// 设备的绑定列表；op 给定时仅返回包含该操作的绑定（空列表 = 无此操作绑定）
    std::vector<const DeviceChannelBinding*> bindings_of_device(
        const core::DeviceId& device_id, std::optional<BindingOp> op = std::nullopt) const;

    /// 资产的点列表
    std::vector<const Point*> points_of_asset(const core::AssetId& asset_id) const;

private:
    void add_issue(std::vector<ValidationIssue>& issues, const char* code,
                   const std::string& object_id, const std::string& field_path,
                   const std::string& reason) const;

    std::unordered_map<std::string, std::size_t> asset_index_;
    std::unordered_map<std::string, std::size_t> device_index_;
    std::unordered_map<std::string, std::size_t> point_index_;
    std::unordered_map<std::string, std::size_t> channel_index_;
    std::unordered_map<std::string, std::size_t> binding_index_;
    std::unordered_map<std::string, std::size_t> capability_index_;
    std::unordered_map<std::string, std::size_t> device_type_index_;
    /// device_id → 绑定下标集合
    std::unordered_map<std::string, std::vector<std::size_t>> device_bindings_index_;
    /// asset_id → 点下标集合
    std::unordered_map<std::string, std::vector<std::size_t>> asset_points_index_;
    std::unordered_set<std::string> mutable issued_codes_;  ///< 校验去重（同对象同码同字段只报一次）

    bool indexed_{false};
};

}  // namespace ems::model
