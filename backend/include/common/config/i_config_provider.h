/**
 * @file i_config_provider.h
 * @brief 配置读取接口（design.md 第 7.2 节）
 *
 * load_version：完整只读快照；Redis 缺失时仅回源同一版本 MySQL，
 * 不能改读"最新"。load_active：带 activation revision 的 active
 * 清单及其精确版本快照。
 */
#pragma once

#include <common/config/config_snapshot.h>
#include <common/core/call_context.h>
#include <common/core/result.h>

#include <memory>
#include <string>

namespace ems::config {

/// active 发布清单（同 scope 原子切换）
struct ActiveManifest {
    std::string scope;
    std::string config_version;
    std::string activation_revision;  ///< 条件切换用的独立激活版本号
};

class IConfigProvider {
   public:
    /** @brief 销毁配置读取接口。 */
    virtual ~IConfigProvider() = default;

    /// 按精确版本加载完整只读快照；缺失时仅回源该版本，NotFound 不得回退"最新"
    /// @param[in] scope 记录作用域。
    /// @param[in] config_version 配置版本。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含配置快照；失败时包含错误信息。
    virtual core::Result<ConfigSnapshotPtr> load_version(const std::string& scope,
                                                         const std::string& config_version,
                                                         const core::CallContext& ctx) = 0;

    /// 加载 active 清单及其精确版本快照
    /// @param[in] scope 记录作用域。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<ActiveManifest> load_active(const std::string& scope,
                                                     const core::CallContext& ctx) = 0;
};

}  // namespace ems::config
