/**
 * @file validation.h
 * @brief 校验结果类型（design.md 第 6.3 节）
 *
 * ValidationIssue 至少含稳定错误码、对象 ID、字段路径、原因；
 * ValidationReport 聚合有上限的字段错误，不返回首个 bool。
 */
#pragma once

#include <string>
#include <vector>

namespace ems::model {

/// 单条校验问题
struct ValidationIssue {
    std::string code;        ///< 稳定错误码（契约的一部分）
    std::string object_id;   ///< 问题对象 ID（asset/device/point/binding...）
    std::string field_path;  ///< 字段路径，如 parent_asset_id
    std::string reason;      ///< 人类可读原因
};

/// 校验报告
struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool truncated{false};  ///< 超过上限被截断

    bool ok() const noexcept { return issues.empty() && !truncated; }
    size_t issue_count() const noexcept { return issues.size(); }
};

/// 常用稳定错误码（契约，只增不改义）
namespace validation_code {
inline constexpr const char* kDuplicateIdentity    = "DuplicateIdentity";     ///< 身份不唯一
inline constexpr const char* kAssetCycle           = "AssetCycle";            ///< 资产成环
inline constexpr const char* kCrossStationParent   = "CrossStationParent";    ///< 跨站挂接
inline constexpr const char* kMissingReference     = "MissingReference";      ///< 引用不存在
inline constexpr const char* kInvalidValue         = "InvalidValue";          ///< 类型/单位/范围非法
inline constexpr const char* kDuplicateBinding     = "DuplicateBinding";      ///< 设备/通道重复绑定
inline constexpr const char* kMissingFeedback      = "MissingFeedback";       ///< 命令缺反馈点
inline constexpr const char* kInvalidFeedbackRef   = "InvalidFeedbackRef";    ///< 反馈引用非法
inline constexpr const char* kDisabledCapability   = "DisabledCapability";    ///< 能力禁用/缺失
inline constexpr const char* kDuplicateRoute       = "DuplicateRoute";        ///< 主来源不唯一/路由重复
inline constexpr const char* kVersionMismatch      = "VersionMismatch";       ///< 版本不闭合
}  // namespace validation_code

}  // namespace ems::model
