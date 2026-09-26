/**
 * @file catalog_validator.h
 * @brief 配置校验默认实现（design.md 第 7.2 节）
 *
 * 复用公共模型的纯校验（ModelCatalog::build_indexes），不执行设备
 * I/O；协议专有配置由受控 schema/校验器另行验证。
 */
#pragma once

#include <common/config/config_validator.h>

namespace ems::config {

/// 默认校验器：调用候选目录的索引构建与纯模型校验
class CatalogValidator final : public IConfigValidator {
   public:
    /// 校验候选完整快照；聚合有上限的字段错误，不返回首个 bool
    /// @param[in] candidate 候选配置。
    /// @return 得到的校验报告。
    model::ValidationReport validate(const model::ModelCatalog& candidate) override;
};

}  // namespace ems::config
