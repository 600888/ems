/**
 * @file config_validator.h
 * @brief 配置校验接口（design.md 第 7.2 节）
 *
 * validate 接收候选完整快照，返回 ValidationReport；不执行设备 I/O。
 * 协议专有配置由受控 schema/校验器验证；通过静态校验不等于驱动已
 * 完成现场能力验收。共享配置校验不替代请求鉴权。
 */
#pragma once

#include <common/model/catalog.h>
#include <common/model/validation.h>

namespace ems::config {

class IConfigValidator {
   public:
    virtual ~IConfigValidator() = default;

    /// 校验候选完整快照；聚合有上限的字段错误，不返回首个 bool
    virtual model::ValidationReport validate(const model::ModelCatalog& candidate) = 0;
};

}  // namespace ems::config
