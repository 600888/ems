/**
 * @file catalog.h
 * @brief 模型聚合目录：构建时建立索引的只读集合（design.md 第 6.2/6.3 节）
 *
 * 公共模型采用值对象、稳定 ID 引用和只读集合，不用双向 shared_ptr
 * 拼出可变对象图。聚合配置在构建时建立索引；发布后通过
 * shared_ptr<const ConfigSnapshot> 共享，读取者持有旧版本期间旧对象保持有效。
 */
#pragma once

#include <common/core/identity.h>
#include <common/model/asset.h>
#include <common/model/binding.h>
#include <common/model/capability.h>
#include <common/model/channel.h>
#include <common/model/device.h>
#include <common/model/point.h>
#include <common/model/validation.h>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ems::model {

/// 一份完整候选/生效配置的模型聚合
class ModelCatalog {
   public:
    // ---- 数据集合（发布前可填充；build_indexes 后视作只读）----
    std::vector<AssetNode> assets;
    std::vector<EssUnitInfo> ess_units;
    std::vector<DeviceTypeDefinition> device_types;
    std::vector<Device> devices;
    /// 四遥点分四个集合保存，字段按类型拆分，不共用可空列
    std::vector<YcPoint> points_yc;
    std::vector<YxPoint> points_yx;
    std::vector<YkPoint> points_yk;
    std::vector<YtPoint> points_yt;
    std::vector<ChannelConfig> channels;
    std::vector<DeviceChannelBinding> bindings;
    std::vector<CapabilityDefinition> capabilities;
    std::vector<VendorPointRoute> vendor_routes;
    std::vector<ForwardingPointMap> forwarding_maps;

    /**
     * @brief 追加一个遥测点。
     * @param[in] point 遥测点。
     */
    void add_point(YcPoint point) { points_yc.push_back(std::move(point)); }

    /**
     * @brief 追加一个遥信点。
     * @param[in] point 遥信点。
     */
    void add_point(YxPoint point) { points_yx.push_back(std::move(point)); }

    /**
     * @brief 追加一个遥控点。
     * @param[in] point 遥控点。
     */
    void add_point(YkPoint point) { points_yk.push_back(std::move(point)); }

    /**
     * @brief 追加一个遥调点。
     * @param[in] point 遥调点。
     */
    void add_point(YtPoint point) { points_yt.push_back(std::move(point)); }

    /**
     * @brief 按变体类型追加四遥点。
     * @param[in] point 任意四遥点。
     */
    void add_point(AnyPoint point) {
        std::visit([this](auto&& p) { add_point(std::forward<decltype(p)>(p)); },
                   std::move(point));
    }

    /**
     * @brief 按 遥测/遥信/遥控/遥调 顺序遍历全部点。
     * @param[in] fn 接收 const PointBase& 的回调。
     */
    template <typename Fn>
    void for_each_point(Fn&& fn) const {
        for (const auto& p : points_yc) fn(static_cast<const PointBase&>(p));
        for (const auto& p : points_yx) fn(static_cast<const PointBase&>(p));
        for (const auto& p : points_yk) fn(static_cast<const PointBase&>(p));
        for (const auto& p : points_yt) fn(static_cast<const PointBase&>(p));
    }

    /**
     * @brief 返回四遥点总数。
     * @return 点数量。
     */
    std::size_t point_count() const noexcept {
        return points_yc.size() + points_yx.size() + points_yk.size() + points_yt.size();
    }

    /**
     * @brief 构建索引并执行纯模型校验（design.md 6.3 validate_snapshot）
     * @return 聚合校验报告（身份唯一、资产无环、引用存在、类型/单位、
     *         多对多绑定、能力依赖、路由引用、版本闭合等）
     */
    ValidationReport build_indexes();

    /**
     * @brief 判断聚合目录是否已完成索引构建。
     * @return true 表示操作成功；false 表示失败。
     */
    bool indexed() const noexcept { return indexed_; }

    /**
     * @brief 按稳定资产 ID 查找；只查询内存索引。
     * @param[in] id 对象标识。
     * @return 匹配的对象指针；未找到时为空。
     */
    const AssetNode* find_asset(const core::AssetId& id) const;

    /**
     * @brief 按稳定设备 ID 查找；不存在返回 nullptr。
     * @param[in] id 对象标识。
     * @return 匹配的对象指针；未找到时为空。
     */
    const Device* find_device(const core::DeviceId& id) const;

    /**
     * @brief 按稳定点 ID 查找；不存在返回 nullptr。
     * @param[in] id 对象标识。
     * @return 匹配的公共字段视图；具体四遥字段可用 point_type 判型后 visit。
     */
    const PointBase* find_point(const core::PointId& id) const;

    /**
     * @brief 按稳定通道 ID 查找；不存在返回 nullptr。
     * @param[in] id 对象标识。
     * @return 匹配的对象指针；未找到时为空。
     */
    const ChannelConfig* find_channel(const core::ChannelId& id) const;

    /**
     * @brief 按绑定 ID 查找；不存在返回 nullptr。
     * @param[in] binding_id binding_id 的标识。
     * @return 匹配的对象指针；未找到时为空。
     */
    const DeviceChannelBinding* find_binding(const std::string& binding_id) const;

    /**
     * @brief 按能力编码查找；不存在返回 nullptr。
     * @param[in] capability_code capability_code 的编码。
     * @return 匹配的对象指针；未找到时为空。
     */
    const CapabilityDefinition* find_capability(const std::string& capability_code) const;

    /**
     * @brief 按设备类型版本 ID 查找；不存在返回 nullptr。
     * @param[in] type_version_id type_version_id 的标识。
     * @return 匹配的对象指针；未找到时为空。
     */
    const DeviceTypeDefinition* find_device_type(const std::string& type_version_id) const;

    /// 设备的绑定列表；op 给定时仅返回包含该操作的绑定（空列表 = 无此操作绑定）
    /// @param[in] device_id 设备 ID。
    /// @param[in] op 数据库操作。
    /// @return 对应对象的指针。
    std::vector<const DeviceChannelBinding*> bindings_of_device(
        const core::DeviceId& device_id, std::optional<BindingOp> op = std::nullopt) const;

    /// 设备的点列表（四遥混合，按 遥测/遥信/遥控/遥调 顺序）
    /// @param[in] asset_id asset_id 的标识。
    /// @return 公共字段视图列表。
    std::vector<const PointBase*> points_of_asset(const core::AssetId& asset_id) const;

   private:
    /**
     * @brief 将校验问题去重后加入报告。
     * @param[in,out] issues 校验问题列表。
     * @param[in] code 对象编码。
     * @param[in] object_id object_id 的标识。
     * @param[in] field_path 字段路径。
     * @param[in] reason 状态或失败原因。
     */
    void add_issue(std::vector<ValidationIssue>& issues, const char* code,
                   const std::string& object_id, const std::string& field_path,
                   const std::string& reason) const;

    /// 四遥点的统一索引项：记录类型与所属集合下标。
    /// 不存裸指针，保证 ModelCatalog 被拷贝/移动后索引仍然有效。
    struct PointSlot {
        PointType type{PointType::YC};  ///< 四遥分类
        std::size_t index{0};           ///< 对应 points_* 集合下标
    };

    /**
     * @brief 把索引项还原为公共字段视图。
     * @param[in] slot 索引项。
     * @return 点指针；下标越界返回 nullptr。
     */
    const PointBase* point_at(const PointSlot& slot) const;

    std::unordered_map<std::string, std::size_t> asset_index_;
    std::unordered_map<std::string, std::size_t> device_index_;
    std::unordered_map<std::string, PointSlot> point_index_;
    std::unordered_map<std::string, std::size_t> channel_index_;
    std::unordered_map<std::string, std::size_t> binding_index_;
    std::unordered_map<std::string, std::size_t> capability_index_;
    std::unordered_map<std::string, std::size_t> device_type_index_;
    /// device_id → 绑定下标集合
    std::unordered_map<std::string, std::vector<std::size_t>> device_bindings_index_;
    /// asset_id → 点索引项集合
    std::unordered_map<std::string, std::vector<PointSlot>> asset_points_index_;
    std::unordered_set<
        std::string> mutable issued_codes_;  ///< 校验去重（同对象同码同字段只报一次）

    bool indexed_{false};
};

}  // namespace ems::model
