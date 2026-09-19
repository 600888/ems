/**
 * @file model_view.h
 * @brief 只读模型视图（design.md 第 6.3 节）
 *
 * 首期不增加万能 DeviceManager。ModelView 从固定版本快照提供内存
 * 查询操作：find_device/find_point/list_bindings/resolve_capability。
 * 不隐式查询数据库补齐；缺失返回明确结果。
 */
#pragma once

#include <common/core/identity.h>
#include <common/model/binding.h>
#include <common/model/capability.h>
#include <common/model/catalog.h>
#include <common/model/device.h>
#include <common/model/point.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ems::model {

/// 能力解析结果：缺失/禁用依赖给出明确原因
struct CapabilityResolution {
    bool ok{false};
    std::string reason;  ///< 失败原因（ok 时为空）
    const CapabilityDefinition* capability{nullptr};
    std::vector<const Point*> required_points;     ///< 启用点
    std::vector<const Point*> required_feedbacks;  ///< 反馈定义
};

/// 只读模型视图：持有一份固定版本快照
class ModelView {
   public:
    explicit ModelView(std::shared_ptr<const ModelCatalog> catalog);

    /// device_id → Device 或 nullptr（NotFound；不回源数据库补齐）
    const Device* find_device(const core::DeviceId& device_id) const;

    /// point_id → Point 或 nullptr（不以 point_code 跨设备查找）
    const Point* find_point(const core::PointId& point_id) const;

    /// device_id + 操作 → 对应绑定列表（空列表表示无此操作绑定）
    std::vector<const DeviceChannelBinding*> list_bindings(
        const core::DeviceId& device_id, std::optional<BindingOp> op = std::nullopt) const;

    /// device_id + capability_code → 启用点、命令与反馈定义；缺失/禁用依赖给出明确原因
    CapabilityResolution resolve_capability(const core::DeviceId& device_id,
                                            const std::string& capability_code) const;

    /// 底层目录（只读）
    const ModelCatalog& catalog() const noexcept { return *catalog_; }

   private:
    std::shared_ptr<const ModelCatalog> catalog_;
};

}  // namespace ems::model
