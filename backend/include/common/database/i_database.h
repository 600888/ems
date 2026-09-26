/**
 * @file i_database.h
 * @brief 数据库连接入口接口（design.md 第 8.1 节）
 *
 * begin 在 deadline 内独占取得连接，返回不可复制的 ITransaction。
 * 连接池本身支持线程安全借还，单个连接/事务仅由一个线程使用；
 * 事务不可跨进程传递。每个进程独立建立连接池。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/database/i_transaction.h>

#include <memory>

namespace ems::database {

class IDatabase {
   public:
    /** @brief 销毁数据库连接池接口。 */
    virtual ~IDatabase() = default;

    /**
     * @brief 在 deadline 内独占取得连接并开启事务；池耗尽或超时返回错误。
     * @param[in] ctx 调用上下文，包含截止时间等信息。
     * @return 成功时包含结果；失败时包含错误信息。
     */
    virtual core::Result<std::unique_ptr<ITransaction>> begin(const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
