#pragma once

#include <common/database/collector_commands.h>
#include <rtdb/client/command_client.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "command_router.h"

namespace ems::net::collect {

/** @brief 从实时库认领控制命令，以关系数据库保留执行登记与重启对账。 */
class CommandService {
   public:
    /**
     * @brief 装配通用事务与命令仓储；引擎须比本对象及全部在途任务存活更久。
     * @param[in] database 关系数据库事务入口。
     * @param[in] repository 同一事务域的命令仓储。
     * @param[in,out] engine 命令所属采集引擎。
     * @param[in] scope 稳定采集作用域，每个作用域只部署一个命令执行实例。
     * @param[in] realtime 同一作用域的实时库命令接口，须比全部在途任务存活更久。
     */
    CommandService(std::shared_ptr<database::IDatabase> database,
                   std::shared_ptr<database::ICollectorCommandRepository> repository,
                   CommandRouter& engine, std::string scope,
                   std::shared_ptr<rtdb::CommandClient> realtime);
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
     * @brief 启动时同步持久未决和既有实时记录，把旧 Executing 变为 Unknown，不重发。
     * @return 数据库记录成功扫描并收敛时为 true，失败时不得开放入口。
     */
    bool recover();

    /**
     * @brief 扫描实时库命令、补同步持久结果并分页调度；由主循环串行调用。
     * @return 实时库和数据库扫描及补同步成功时为 true。
     */
    bool poll();

    /**
     * @brief 订阅线程登记待检查的命令 ID；提示可重复或丢失，不在此处执行或读取载荷。
     * @param[in] command_id 通知中的稳定命令 ID；非法或队列满时忽略，由扫描补偿。
     */
    void notify(const std::string& command_id);

    /**
     * @brief 热加载/停机前暂停命令调度与发送；生产者仍可写入实时库等待恢复。
     * @param[in] accepting true 恢复受理，false 暂停受理及物理发送。
     */
    void set_accepting(bool accepting);

    /**
     * @brief 登记一条北向服务端控制，仅供 poll 所在的调度线程受理。
     *
     * 不在此线程访问数据库：请求进入有界队列，由 poll 转成与 ems-command
     * 完全相同的持久记录、路由与执行回执。
     *
     * @param[in] channel_id 受理该控制的逻辑通道 ID，用于生成稳定命令 ID。
     * @param[in] control 协议插件给出的控制请求。
     * @param[in] done 执行结果回执；注册成功时恰好调用一次，注册失败时立即以失败调用。
     * @return true 已登记；false 队列已满或已停止受理，done 已同步调用。
     */
    bool submit_peer(std::int64_t channel_id, const ChannelThread::PeerControl& control,
                     ChannelThread::PeerOutcomeHandler done);

   private:
    struct Journal;

    /** @brief 待受理的北向控制；只在 poll 线程访问数据库。 */
    struct PeerRequest {
        std::int64_t channel_id{0};
        ChannelThread::PeerControl control;
        ChannelThread::PeerOutcomeHandler done;
    };

    struct Active {
        std::mutex mutex;
        std::set<std::string> ids;
        std::map<std::string, ChannelThread::PeerOutcomeHandler> peers;
    };

    /**
     * @brief 把队列中的北向控制转成持久命令并调度。
     * @return 本次队列可用时为 true。
     */
    bool drain_peers();

    /**
     * @brief 受理单条北向控制；失败时立即回执负结果。
     * @param[in] request 已出队的请求。
     */
    void accept_peer(PeerRequest request);

    /**
     * @brief 安排一次物理发送或只读核验。
     * @param[in] row 已持久保存的命令。
     * @param[in] verify 是否仅核验。
     * @return 成功排队或已经在途时为 true。
     */
    bool schedule(const database::CollectorCommandRow& row, bool verify);

    /**
     * @brief 将实时库中的 Pending 命令登记为持久待发记录，不执行设备 I/O。
     * @param[in] command 从实时库读取的完整命令。
     * @return 登记及状态同步成功时为 true。
     */
    bool ingest(const rtdb::CommandRecord& command);

    std::shared_ptr<Journal> journal_;
    std::shared_ptr<Active> active_{std::make_shared<Active>()};
    std::shared_ptr<std::atomic<bool>> accepting_{std::make_shared<std::atomic<bool>>(false)};
    CommandRouter& engine_;
    std::int64_t cursor_{0};
    std::size_t realtime_cursor_{0};
    std::mutex hint_mutex_;
    std::set<std::string> hints_;

    // 北向控制：通道线程只入队，poll 线程受理。
    std::mutex peer_mutex_;
    std::deque<PeerRequest> peer_queue_;
    std::string peer_prefix_;
};
}  // namespace ems::net::collect
