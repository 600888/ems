#pragma once

#include <common/database/collector_commands.h>
#include <common/messaging/zmq_command_channel.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include "command_router.h"

namespace ems::net::collect {

/** @brief 通用关系数据库上的命令 inbox、执行登记与结果查询，不依赖具体数据库驱动。 */
class CommandService {
   public:
    /**
     * @brief 装配通用事务与命令仓储；引擎须比本对象及全部在途任务存活更久。
     * @param[in] database 关系数据库事务入口。
     * @param[in] repository 同一事务域的命令仓储。
     * @param[in,out] engine 命令所属采集引擎。
     * @param[in] scope 稳定采集作用域，每个作用域只部署一个命令执行实例。
     */
    CommandService(std::shared_ptr<database::IDatabase> database,
                   std::shared_ptr<database::ICollectorCommandRepository> repository,
                   CommandRouter& engine, std::string scope);
    /** @brief 禁止后续入队；已发任务由通道线程收尾。 */
    ~CommandService();
    /** @brief 禁止复制服务及其受理开关。
     * @param[in] other 源服务。
     */
    CommandService(const CommandService& other) = delete;
    /** @brief 禁止复制赋值。
     * @param[in] other 源服务。
     * @return 不可调用。
     */
    CommandService& operator=(const CommandService& other) = delete;

    /**
     * @brief 启动时把旧 Executing 记录变为 Unknown，绝不自动重发。
     * @return 数据库记录成功扫描并收敛时为 true，失败时不得开放入口。
     */
    bool recover();

    /**
     * @brief 校验并持久受理 submit，或处理 query/verify；不等待设备执行。
     * @param[in] request 稳定 ID 和业务 JSON；同 ID 异载荷拒绝。
     * @return 受理状态；Result/Accepted/Duplicate 的 reason 包含结果 JSON。
     */
    messaging::ZmqCommandReply handle(const messaging::ZmqCommandRequest& request);

    /**
     * @brief 分页调度持久待发命令及只读反馈核验；由主循环串行调用。
     * @return 本次数据库扫描成功时为 true。
     */
    bool poll();

    /**
     * @brief 热加载/停机前暂停新命令；查询仍可用。
     * @param[in] accepting true 恢复受理，false 暂停受理及物理发送。
     */
    void set_accepting(bool accepting);

   private:
    struct Journal;

    struct Active {
        std::mutex mutex;
        std::set<std::string> ids;
    };

    /**
     * @brief 安排一次物理发送或只读核验。
     * @param[in] row 已持久保存的命令。
     * @param[in] verify 是否仅核验。
     * @return 成功排队或已经在途时为 true。
     */
    bool schedule(const database::CollectorCommandRow& row, bool verify);

    std::shared_ptr<Journal> journal_;
    std::shared_ptr<Active> active_{std::make_shared<Active>()};
    std::shared_ptr<std::atomic<bool>> accepting_{std::make_shared<std::atomic<bool>>(false)};
    CommandRouter& engine_;
    std::int64_t cursor_{0};
};
}  // namespace ems::net::collect
