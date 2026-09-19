/**
 * @file i_database.h
 * @brief 数据库连接入口接口（design.md 第 8.1 节）
 *
 * begin 在 deadline 内独占取得连接，返回不可复制的 ITransaction。
 * 连接池本身支持线程安全借还，单个连接/事务仅由一个线程使用；
 * 事务不可跨进程传递。每个进程独立建立连接池。
 */
#pragma once

#include <memory>

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/database/i_transaction.h>

namespace ems::database {

class IDatabase {
public:
    virtual ~IDatabase() = default;

    /// 在 deadline 内独占取得连接并开启事务；池耗尽/超时返回明确错误
    virtual core::Result<std::unique_ptr<ITransaction>> begin(const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
