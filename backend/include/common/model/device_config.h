/**
 * @file device_config.h
 * @brief 设备固有点目录、实例配置与发布前纯模型校验。
 */
#pragma once

#include <common/model/catalog.h>

#include <string>
#include <vector>

namespace ems::model {

/// 固有点配置状态，与运行质量分离。
enum class StandardPointState { Unconfigured, Enabled, Disabled };

/// 一个设备实例的固有点配置；point_id 在重新导入时保持稳定。
struct DeviceStandardPointConfig {
    core::DeviceId device_id;
    core::PointId point_id;
    std::string standard_code;
    StandardPointState state{StandardPointState::Unconfigured};
    std::string disabled_reason;
};

/// 类型级能力依赖，以固有点 code 表达，避免跨设备引用点 ID。
struct StandardCapability {
    std::string code;
    std::vector<std::string> required_codes;
};

/// 设备配置草稿及其类型级能力依赖。
struct DevicePointDraft {
    core::DeviceId device_id;
    std::vector<DeviceStandardPointConfig> points;
    std::vector<StandardCapability> capabilities;
};

/// 固有能力的静态可用性；不包含实时质量、授权或策略判定。
struct StandardCapabilityStatus {
    bool available{false};
    std::vector<std::string> reasons;
};

/**
 * @brief 校验设备固有点草稿或待发布配置，不进行 I/O 或设备写入。
 * @param[in] catalog 已构建索引的候选模型目录。
 * @param[in] draft 待校验的设备实例配置。
 * @param[in] publishing true 时要求全部固有点已明确启用或禁用，并验证可执行路由。
 * @return 精确到对象和字段的错误报告；报告非空时禁止发布。
 * @note 校验通过不代表命令编码、反馈、协议驱动或跨进程版本已可投运。
 */
ValidationReport validate_device_point_draft(const ModelCatalog& catalog,
                                             const DevicePointDraft& draft, bool publishing);

/**
 * @brief 按固有点启用状态解析设备的静态能力。
 * @param[in] draft 设备实例配置。
 * @param[in] capability_code 需要解析的能力 code。
 * @return 静态可用性及缺失、未配置或禁用的具体原因。
 */
StandardCapabilityStatus resolve_standard_capability(const DevicePointDraft& draft,
                                                     const std::string& capability_code);

}  // namespace ems::model
