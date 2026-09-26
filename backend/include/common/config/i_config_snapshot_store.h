/**
 * @file i_config_snapshot_store.h
 * @brief 配置快照存储接口（design.md 第 7.2 节）
 *
 * put_if_absent：同版本同内容幂等；同版本不同内容 Conflict。
 * activate：条件切换 active，并发冲突不能后写覆盖。
 * 数据库必须能读取同版本发布载荷，或按版本化确定性编码规则
 * 重建完全一致的字节。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>

#include <string>

namespace ems::config {

/// 原始不可变快照字节与校验和
struct SnapshotBytes {
    std::string bytes;     ///< 发布方保存并发送的同一份 payload UTF-8 字节
    std::string checksum;  ///< SHA-256 hex（对 bytes 计算）
};

class IConfigSnapshotStore {
   public:
    /** @brief 销毁配置快照存储接口。 */
    virtual ~IConfigSnapshotStore() = default;

    /// 读取原始不可变快照与 checksum
    /// @param[in] scope 记录作用域。
    /// @param[in] version 版本号。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<SnapshotBytes> get(const std::string& scope, const std::string& version,
                                            const core::CallContext& ctx) = 0;

    /// 同版本同内容幂等写入；同版本不同内容返回 Conflict
    /// @param[in] scope 记录作用域。
    /// @param[in] version 版本号。
    /// @param[in] bytes 输入字节序列。
    /// @param[in] checksum 预期的校验和。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status put_if_absent(const std::string& scope, const std::string& version,
                                       const std::string& bytes, const std::string& checksum,
                                       const core::CallContext& ctx) = 0;

    /// 条件切换 active；expected_activation_revision 不符返回 Conflict，不后写覆盖
    /// @param[in] scope 记录作用域。
    /// @param[in] version 版本号。
    /// @param[in] expected_activation_revision 期望的激活修订号。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status activate(const std::string& scope, const std::string& version,
                                  const std::string& expected_activation_revision,
                                  const core::CallContext& ctx) = 0;
};

}  // namespace ems::config
