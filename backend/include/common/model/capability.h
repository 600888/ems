/**
 * @file capability.h
 * @brief 能力定义（design.md 第 6.1 节）
 *
 * CapabilityDefinition 表达配置能力（所需点/反馈、参数及单位约束），
 * 不保证当前允许执行；执行允许由策略仲裁。
 */
#pragma once

#include <string>
#include <vector>

#include <common/core/identity.h>
#include <common/model/point.h>

namespace ems::model {

/// 能力参数约束（带单位与范围）
struct CapabilityParameter {
    std::string name;                         ///< 参数名
    ValueType value_type{ValueType::Double};  ///< 参数值类型
    std::string unit;                         ///< 单位
    bool has_range{false};                    ///< 是否限定范围
    double min_value{0.0};
    double max_value{0.0};
};

/// 能力定义（版本化配置）
struct CapabilityDefinition {
    std::string capability_code;                  ///< 能力编码，如 charge / discharge
    std::string name;
    std::vector<core::PointId> required_points;    ///< 所需标准点
    std::vector<core::PointId> required_feedbacks; ///< 所需命令反馈点
    std::vector<CapabilityParameter> parameters;   ///< 参数及单位约束
    bool enabled{true};
};

}  // namespace ems::model
