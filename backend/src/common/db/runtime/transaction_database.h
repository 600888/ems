#pragma once

#include <common/database/i_database.h>

#include <chrono>
#include <functional>
#include <memory>

namespace ems::database::detail {

/** @brief 公共运行时内部连接接口；不向业务暴露驱动。 */
class Connection {
   public:
    /** @brief 释放原生连接。 */
    virtual ~Connection() = default;
    /**
     * @brief 开启单连接事务，不允许驱动自动重连恢复事务。
     * @param[in] ctx 调用期限。
     * @return 开启结果。
     */
    virtual core::Status begin(const core::CallContext& ctx) = 0;
    /**
     * @brief 提交事务；传输错误必须保留 Unknown 语义。
     * @param[in] ctx 调用期限。
     * @return 提交结果。
     */
    virtual core::Status commit(const core::CallContext& ctx) = 0;
    /**
     * @brief 回滚未提交事务。
     * @param[in] ctx 清理期限。
     * @return 回滚结果；失败后不得复用连接。
     */
    virtual core::Status rollback(const core::CallContext& ctx) = 0;
};

struct TransactionOptions {
    std::chrono::milliseconds cleanup_timeout{1000};
};

/** @brief 独占借用的连接；销毁前通过归还回调交还底层库的连接池。 */
struct ConnectionLease {
    std::shared_ptr<Connection> connection;
    std::function<void(std::shared_ptr<Connection>, bool)> release;
};

using ConnectionFactory = std::function<core::Result<ConnectionLease>(const core::CallContext&)>;

/** @brief 显式事务适配；连接借还由注入的库连接池负责，不实现池队列。 */
class TransactionDatabase final : public IDatabase {
   public:
    /**
     * @brief 注入底层池的借用入口；不会访问数据库。
     * @param[in] options 事务清理预算。
     * @param[in] factory 从库连接池借出连接及归还回调，回调不得抛异常。
     */
    TransactionDatabase(TransactionOptions options, ConnectionFactory factory);
    /**
     * @brief 禁止复制事务运行时。
     * @param[in] other 原运行时。
     */
    TransactionDatabase(const TransactionDatabase& other) = delete;
    /**
     * @brief 禁止复制赋值事务运行时。
     * @param[in] other 原运行时。
     * @return 不可调用。
     */
    TransactionDatabase& operator=(const TransactionDatabase& other) = delete;
    /** @brief 拒绝新事务；在途事务仍可结束并归还底层池。 */
    ~TransactionDatabase() override;
    /**
     * @brief 在预算内借出独占连接并开启事务。
     * @param[in] ctx 调用期限。
     * @return 活动事务；借用期限由底层池适配器执行，关闭后返回 Unavailable。
     */
    core::Result<std::unique_ptr<ITransaction>> begin(const core::CallContext& ctx) override;
    /**
     * @brief 验证事务归属、线程、活动状态和期限后借用内部连接。
     * @param[in,out] tx 调用方事务。
     * @param[in] ctx 调用期限。
     * @return 仅在本次事务存活时有效的连接指针；跨上下文或线程拒绝。
     */
    core::Result<Connection*> connection(ITransaction& tx, const core::CallContext& ctx);
    /**
     * @brief 标记事务不可提交，结束时丢弃连接。
     * @param[in,out] tx 本上下文事务，其他上下文事务不受影响。
     */
    void invalidate(ITransaction& tx);
    /** @brief 拒绝新事务；正在借用的调用在借用预算内退出，不关闭进程内共享池。 */
    void close();

   private:
    struct State;
    class Transaction;
    std::shared_ptr<State> state_;
};

}  // namespace ems::database::detail
