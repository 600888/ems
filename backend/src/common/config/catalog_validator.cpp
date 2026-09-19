/**
 * @file catalog_validator.cpp
 * @brief 配置校验默认实现
 */
#include <common/config/catalog_validator.h>

namespace ems::config {

model::ValidationReport CatalogValidator::validate(const model::ModelCatalog& candidate) {
    // build_indexes 同时完成索引构建与纯模型校验（design.md 6.3）；
    // 候选目录即将发布，构建索引是其发布前处理的一部分
    model::ModelCatalog& mutable_candidate = const_cast<model::ModelCatalog&>(candidate);
    return mutable_candidate.build_indexes();
}

}  // namespace ems::config
