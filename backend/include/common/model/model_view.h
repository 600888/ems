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
    /**
     * @brief 持有一份固定版本目录作为只读查询视图。
     * @param[in] catalog 模型目录。
     */
    explicit ModelView(std::shared_ptr<const ModelCatalog> catalog);

    /// device_id → Device 或 nullptr（NotFound；不回源数据库补齐）
    /// @param[in] device_id 设备 ID。
    /// @return 匹配的对象指针；未找到时为空。
    const Device* find_device(const core::DeviceId& device_id) const;

    /// point_id → Point 或 nullptr（不以 point_code 跨设备查找）
    /// @param[in] point_id point_id 的标识。
    /// @return 匹配的对象指针；未找到时为空。
    const Point* find_point(const core::PointId& point_id) const;

    /// device_id + 操作 → 对应绑定列表（空列表表示无此操作绑定）
    /// @param[in] device_id 设备 ID。
    /// @param[in] op 数据库操作。
    /// @return 对应对象的指针。
    std::vector<const DeviceChannelBinding*> list_bindings(
        const core::DeviceId& device_id, std::optional<BindingOp> op = std::nullopt) const;

    /// device_id + capability_code → 启用点、命令与反馈定义；缺失/禁用依赖给出明确原因
    /// @param[in] device_id 设备 ID。
    /// @param[in] capability_code capability_code 的编码。
    /// @return 得到的能力解析结果。
    CapabilityResolution resolve_capability(const core::DeviceId& device_id,
                                            const std::string& capability_code) const;

    /// 底层目录（只读）
    /// @return 对应对象的引用。
    const ModelCatalog& catalog() const noexcept { return *catalog_; }

   private:
    std::shared_ptr<const ModelCatalog> catalog_;
};

}  // namespace ems::model
