/**
 * @file model_view.cpp
 * @brief 只读模型视图实现（design.md 第 6.3 节）
 */
#include <common/model/model_view.h>

#include <algorithm>

namespace ems::model {

ModelView::ModelView(std::shared_ptr<const ModelCatalog> catalog) : catalog_(std::move(catalog)) {}

const Device* ModelView::find_device(const core::DeviceId& device_id) const {
    return catalog_->find_device(device_id);
}

const PointBase* ModelView::find_point(const core::PointId& point_id) const {
    return catalog_->find_point(point_id);
}

std::vector<const DeviceChannelBinding*> ModelView::list_bindings(
    const core::DeviceId& device_id, std::optional<BindingOp> op) const {
    return catalog_->bindings_of_device(device_id, op);
}

CapabilityResolution ModelView::resolve_capability(const core::DeviceId& device_id,
                                                   const std::string& capability_code) const {
    CapabilityResolution res;

    const Device* device = catalog_->find_device(device_id);
    if (device == nullptr) {
        res.reason = "设备不存在: " + device_id.value();
        return res;
    }

    const DeviceTypeDefinition* type = catalog_->find_device_type(device->type_version_id);
    if (type == nullptr) {
        res.reason = "设备类型版本不存在: " + device->type_version_id;
        return res;
    }

    // 设备类型必须声明该能力
    if (std::find(type->capability_codes.begin(), type->capability_codes.end(), capability_code) ==
        type->capability_codes.end()) {
        res.reason = "设备类型未声明能力: " + capability_code;
        return res;
    }

    const CapabilityDefinition* cap = catalog_->find_capability(capability_code);
    if (cap == nullptr) {
        res.reason = "能力定义缺失: " + capability_code;
        return res;
    }
    if (!cap->enabled) {
        res.reason = "能力已禁用: " + capability_code;
        return res;
    }

    res.capability = cap;

    // 解析启用点；缺失或禁用给出明确原因
    for (const auto& pid : cap->required_points) {
        const PointBase* p = catalog_->find_point(pid);
        if (p == nullptr) {
            res.reason = "能力所需点不存在: " + pid.value();
            res.capability = nullptr;
            res.required_points.clear();
            res.required_feedbacks.clear();
            return res;
        }
        if (!p->enabled) {
            res.reason = "能力所需点已禁用: " + pid.value();
            res.capability = nullptr;
            res.required_points.clear();
            res.required_feedbacks.clear();
            return res;
        }
        res.required_points.push_back(p);
    }

    for (const auto& pid : cap->required_feedbacks) {
        const PointBase* p = catalog_->find_point(pid);
        if (p == nullptr) {
            res.reason = "能力所需反馈点不存在: " + pid.value();
            res.capability = nullptr;
            res.required_points.clear();
            res.required_feedbacks.clear();
            return res;
        }
        if (!p->enabled) {
            res.reason = "能力所需反馈点已禁用: " + pid.value();
            res.capability = nullptr;
            res.required_points.clear();
            res.required_feedbacks.clear();
            return res;
        }
        res.required_feedbacks.push_back(p);
    }

    res.ok = true;
    return res;
}

}  // namespace ems::model
