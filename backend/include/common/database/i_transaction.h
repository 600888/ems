/**
 * @file i_transaction.h
 * @brief 数据库事务接口（design.md 第 8.1 节）
 *
 * ITransaction 不可复制；事务与其创建的 IDatabase 实例绑定，跨实例
 * 传入直接拒绝。事务析构对未提交工作尝试 rollback，若结果不确定则
 * 丢弃连接，不把它归还为健康连接。
 *
 * commit 返回 Receipt 或带确定性的 Error：提交响应丢失返回
 * effect=Unknown，此后不得继续使用该事务，通过原稳定业务 ID 从新
 * 连接对账。关闭后禁止继续调用。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>

namespace ems::database {

/** @brief 成功提交回执；不确定结果由 Unknown 错误表达。 */
struct CommitReceipt {};

/** @brief 与单个数据库连接绑定的不可复制事务接口。 */
class ITransaction {
   public:
    /** @brief 构造未提交事务接口。 */
    ITransaction() = default;

    /** @brief 销毁事务；未提交时由实现尝试回滚。 */
    virtual ~ITransaction() = default;

    /**
     * @brief 禁止复制事务。
     * @param[in] other 待复制或移动的源对象。
     */
    ITransaction(const ITransaction& other) = delete;

    /**
     * @brief 禁止复制赋值事务。
     * @param[in] other 待复制或移动的源对象。
     */
    ITransaction& operator=(const ITransaction& other) = delete;

    /**
     * @brief 提交事务；响应丢失时返回 Unknown，之后不得继续使用该事务。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含结果；失败时包含错误信息。
     */
    virtual core::Result<CommitReceipt> commit(const core::CallContext& ctx) = 0;

    /**
     * @brief 幂等回滚事务。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 得到的操作状态。
     */
    virtual core::Status rollback(const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
