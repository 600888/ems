/**
 * @file config_snapshot.h
 * @brief 配置快照封套与不可变快照（design.md 第 7.2 节）
 *
 * 快照封套包含 schema_version、scope、config_version、generated_at、
 * checksum 及引用清单。checksum 采用 SHA-256，计算对象是封套之外的
 * 实际 payload UTF-8 字节；读取者先校验原字节再解析。
 * 发布后快照通过 shared_ptr<const ConfigSnapshot> 共享，读取者持有
 * 旧版本期间旧对象保持有效。
 */
#pragma once

#include <common/core/identity.h>
#include <common/core/time.h>
#include <common/model/catalog.h>

#include <memory>
#include <string>

namespace ems::config {

/// 快照封套元数据
struct SnapshotEnvelope {
    std::string schema_version;  ///< 快照 schema 版本
    std::string scope;           ///< 逻辑 scope（unit/station code）
    std::string config_version;  ///< 不可变配置版本（只比较相等）
    core::UtcMicros generated_at{core::kInvalidUtcMicros};
    std::string checksum;  ///< SHA-256 hex，对象为封套外 payload UTF-8 字节
};

/// 不可变配置快照（模型聚合 + 封套）
class ConfigSnapshot {
   public:
    ConfigSnapshot(SnapshotEnvelope envelope, model::ModelCatalog catalog)
        : envelope_(std::move(envelope)), catalog_(std::move(catalog)) {}

    const SnapshotEnvelope& envelope() const noexcept { return envelope_; }

    /// 目录必须已调用 build_indexes()（发布/加载方负责校验）
    const model::ModelCatalog& catalog() const noexcept { return catalog_; }

   private:
    SnapshotEnvelope envelope_;
    model::ModelCatalog catalog_;
};

using ConfigSnapshotPtr = std::shared_ptr<const ConfigSnapshot>;

}  // namespace ems::config
