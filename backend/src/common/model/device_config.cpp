/**
 * @file device_config.cpp
 * @brief 设备固有点配置的确定性校验。
 */
#include <common/model/device_config.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace ems::model {
namespace {

bool has_operation(const DeviceChannelBinding& binding, BindingOp operation) {
    return std::find(binding.operations.begin(), binding.operations.end(), operation) !=
           binding.operations.end();
}

bool purpose_allows(ChannelPurpose purpose, BindingOp operation) {
    if (operation == BindingOp::Control) {
        return purpose == ChannelPurpose::Control || purpose == ChannelPurpose::Both;
    }
    return purpose == ChannelPurpose::Telemetry || purpose == ChannelPurpose::Both;
}

void add_issue(ValidationReport& report, const char* code, const std::string& object_id,
               const std::string& field, const std::string& reason) {
    constexpr std::size_t kMaxIssues = 256;
    if (report.issues.size() >= kMaxIssues) {
        report.truncated = true;
        return;
    }
    report.issues.push_back({code, object_id, field, reason});
}

}  // namespace

ValidationReport validate_device_point_draft(const ModelCatalog& catalog,
                                             const DevicePointDraft& draft, bool publishing) {
    ValidationReport report;
    if (!catalog.indexed()) {
        add_issue(report, validation_code::kInvalidValue, draft.device_id.value(), "catalog",
                  "模型目录尚未构建索引");
        return report;
    }
    const Device* device = catalog.find_device(draft.device_id);
    if (device == nullptr) {
        add_issue(report, validation_code::kMissingReference, draft.device_id.value(), "device_id",
                  "设备不存在");
        return report;
    }
    const AssetNode* device_asset = catalog.find_asset(device->asset_id);
    if (device_asset == nullptr) {
        add_issue(report, validation_code::kMissingReference, draft.device_id.value(), "asset_id",
                  "设备资产节点不存在");
        return report;
    }
    const DeviceTypeDefinition* type = catalog.find_device_type(device->type_version_id);
    if (type == nullptr) {
        add_issue(report, validation_code::kVersionMismatch, draft.device_id.value(),
                  "type_version_id", "设备类型版本不存在");
        return report;
    }

    std::unordered_map<std::string, const StandardPointDefinition*> definitions;
    for (const auto& definition : type->standard_points) {
        if (!definitions.emplace(definition.semantic_code, &definition).second) {
            add_issue(report, validation_code::kDuplicateIdentity, type->type_version_id,
                      "standard_points", "固有点 code 重复: " + definition.semantic_code);
        }
    }

    std::unordered_set<std::string> capability_codes;
    for (const auto& capability : draft.capabilities) {
        if (!capability_codes.insert(capability.code).second) {
            add_issue(report, validation_code::kDuplicateIdentity, capability.code, "capabilities",
                      "能力 code 重复");
        }
        if (std::find(type->capability_codes.begin(), type->capability_codes.end(),
                      capability.code) == type->capability_codes.end()) {
            add_issue(report, validation_code::kMissingReference, capability.code, "capabilities",
                      "设备类型未声明该能力");
        }
        if (capability.required_codes.empty()) {
            add_issue(report, validation_code::kInvalidValue, capability.code, "required_codes",
                      "能力依赖不能为空");
        }
        for (const auto& code : capability.required_codes) {
            if (definitions.count(code) == 0) {
                add_issue(report, validation_code::kMissingReference, capability.code,
                          "required_codes", "能力依赖的固有点不存在: " + code);
            }
        }
    }
    if (publishing) {
        for (const auto& code : type->capability_codes) {
            if (capability_codes.count(code) == 0) {
                add_issue(report, validation_code::kMissingReference, draft.device_id.value(),
                          "capabilities", "缺少能力定义: " + code);
            }
        }
    }

    std::unordered_set<std::string> seen_codes;
    std::unordered_set<std::string> seen_ids;
    for (const auto& config : draft.points) {
        const std::string id = config.point_id.value();
        if (config.device_id != draft.device_id) {
            add_issue(report, validation_code::kMissingReference, id, "device_id",
                      "固有点配置属于其他设备");
        }
        if (!seen_codes.insert(config.standard_code).second) {
            add_issue(report, validation_code::kDuplicateIdentity, id, "standard_code",
                      "同一设备的固有点 code 重复");
        }
        if (!seen_ids.insert(id).second) {
            add_issue(report, validation_code::kDuplicateIdentity, id, "point_id",
                      "同一设备的固有点 ID 重复");
        }
        auto definition = definitions.find(config.standard_code);
        if (definition == definitions.end()) {
            add_issue(report, validation_code::kMissingReference, id, "standard_code",
                      "类型目录中不存在该固有点");
            continue;
        }
        const Point* point = catalog.find_point(config.point_id);
        if (point == nullptr) {
            add_issue(report, validation_code::kMissingReference, id, "point_id", "点实例不存在");
            continue;
        }
        if (point->asset_id != device->asset_id) {
            add_issue(report, validation_code::kInvalidValue, id, "asset_id",
                      "首期固有点必须位于设备资产节点");
        }
        if (point->code != config.standard_code) {
            add_issue(report, validation_code::kInvalidValue, id, "code",
                      "点实例 code 与固有点 code 不一致");
        }
        if (point->kind != definition->second->kind ||
            point->value_type != definition->second->value_type ||
            point->unit != definition->second->unit) {
            add_issue(report, validation_code::kInvalidValue, id, "definition",
                      "点实例的种类、值类型或单位与固有定义不一致");
        }
        if (config.state == StandardPointState::Unconfigured) {
            if (publishing) {
                add_issue(report, validation_code::kInvalidValue, id, "state",
                          "未配置的固有点不能发布");
            }
            continue;
        }
        if (config.state == StandardPointState::Disabled) {
            if (config.disabled_reason.empty()) {
                add_issue(report, validation_code::kInvalidValue, id, "disabled_reason",
                          "禁用固有点必须填写原因");
            }
            if (publishing) {
                for (const auto& route : catalog.vendor_routes) {
                    if (route.point_id == point->point_id && route.enabled) {
                        add_issue(report, validation_code::kInvalidValue, id, "vendor_routes",
                                  "禁用固有点仍存在启用的标准投影路由");
                        break;
                    }
                }
            }
            continue;
        }
        if (config.state != StandardPointState::Enabled) {
            add_issue(report, validation_code::kInvalidValue, id, "state", "未知固有点配置状态");
            continue;
        }
        if (!point->enabled) {
            add_issue(report, validation_code::kInvalidValue, id, "enabled",
                      "固有点已配置为启用，但点实例被禁用");
        }
        if (!config.disabled_reason.empty()) {
            add_issue(report, validation_code::kInvalidValue, id, "disabled_reason",
                      "启用固有点不能保留禁用原因");
        }

        const BindingOp required_op =
            point->kind == PointKind::Command ? BindingOp::Control : BindingOp::Collect;
        std::size_t valid_routes = 0;
        for (const auto& route : catalog.vendor_routes) {
            if (route.point_id != point->point_id || !route.enabled) continue;
            const DeviceChannelBinding* binding = catalog.find_binding(route.binding_id);
            const ChannelConfig* channel =
                binding ? catalog.find_channel(binding->channel_id) : nullptr;
            if (binding == nullptr || channel == nullptr || binding->device_id != draft.device_id ||
                !binding->enabled || !channel->enabled || !has_operation(*binding, required_op) ||
                !purpose_allows(channel->purpose, required_op) ||
                channel->station_id != device_asset->station_id || route.vendor_address.empty() ||
                route.writable != (required_op == BindingOp::Control)) {
                add_issue(report, validation_code::kInvalidValue, route.route_id, "binding_id",
                          "固有点路由的设备、通道、操作或读写权限不匹配");
                continue;
            }
            if (!std::isfinite(route.scale) || !std::isfinite(route.offset) || route.scale == 0.0) {
                add_issue(report, validation_code::kInvalidValue, route.route_id, "scale/offset",
                          "转换系数必须有限且倍率非零");
                continue;
            }
            ++valid_routes;
        }
        if (publishing && valid_routes != 1) {
            add_issue(report, validation_code::kDuplicateRoute, id, "vendor_routes",
                      "启用固有点发布时必须恰有一个有效的权威路由");
        }
    }
    if (publishing) {
        for (const auto& definition : type->standard_points) {
            if (seen_codes.count(definition.semantic_code) == 0) {
                add_issue(report, validation_code::kMissingReference, draft.device_id.value(),
                          "points", "缺少固有点配置: " + definition.semantic_code);
            }
        }
    }
    return report;
}

StandardCapabilityStatus resolve_standard_capability(const DevicePointDraft& draft,
                                                     const std::string& capability_code) {
    StandardCapabilityStatus status;
    const StandardCapability* capability = nullptr;
    for (const auto& candidate : draft.capabilities) {
        if (candidate.code == capability_code) {
            capability = &candidate;
            break;
        }
    }
    if (capability == nullptr) {
        status.reasons.push_back("能力未声明: " + capability_code);
        return status;
    }
    for (const auto& code : capability->required_codes) {
        auto point = std::find_if(draft.points.begin(), draft.points.end(),
                                  [&code](const DeviceStandardPointConfig& config) {
                                      return config.standard_code == code;
                                  });
        if (point == draft.points.end()) {
            status.reasons.push_back("缺少固有点: " + code);
        } else if (point->state == StandardPointState::Unconfigured) {
            status.reasons.push_back("固有点未配置: " + code);
        } else if (point->state == StandardPointState::Disabled) {
            status.reasons.push_back("固有点已禁用: " + code);
        }
    }
    status.available = status.reasons.empty();
    return status;
}

}  // namespace ems::model
