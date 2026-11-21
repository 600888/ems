#pragma once

#include <common/database/collector_commands.h>
#include <rtdb/client/command_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

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
    /** @brief 停止并等待受理工作线程；已发任务由通道线程收尾。 */
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
     * @brief 扫描实时库及持久未决记录，将受理和补同步分配到设备队列；由主循环调用。
     * @return 本轮扫描及已完成受理无错误时为 true；慢设备任务继续在后台处理。
     */
    bool poll();

    /**
     * @brief 订阅线程登记待检查的命令 ID；提示可重复或丢失，不在此处执行或读取载荷。
     * @param[in] command_id 通知中的稳定命令 ID；非法或队列满时忽略，由扫描补偿。
     */
    void notify(const std::string& command_id);

    /**
     * @brief 在暂停期间按当前运行目录注册设备逻辑订阅，替换旧主题映射。
     * @param[in] catalog 当前完整目录；按控制绑定设备分组。
     * @return 用于同一个 SUB socket 的精确主题集合，包含未路由命令主题。
     */
    std::vector<std::string> configure_subscriptions(const rtdb::RuntimeCatalog& catalog);

    /**
     * @brief 接收线程将设备通知分发到对应有界队列，不执行数据库或协议 I/O。
     * @param[in] topic 已注册的设备主题；未知主题忽略。
     * @param[in] command_id 实时库命令 ID，实际目标在工作线程重新读取和核对。
     */
    void notify(const std::string& topic, const std::string& command_id);

    /**
     * @brief 热加载/停机前暂停命令调度与发送；生产者仍可写入实时库等待恢复。
     * @param[in] accepting true 恢复受理，false 暂停受理及物理发送。
     */
    void set_accepting(bool accepting);

    /**
     * @brief 登记一条北向服务端控制，仅供 poll 所在的调度线程受理。
     *
     * 不在此线程访问数据库：请求进入有界队列，由 poll 转成与 ems-command
     * 相同的实时命令，并交给设备队列进行持久登记、路由与执行回执。
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

    /** @brief 待受理的北向控制；poll 先写实时库，再交给设备工作队列。 */
    struct PeerRequest {
        std::int64_t channel_id{0};
        ChannelThread::PeerControl control;
        ChannelThread::PeerOutcomeHandler done;
        std::int64_t config_revision{0};
    };

    struct Active {
        std::mutex mutex;
        std::set<std::string> ids;
        std::map<std::string, std::string> devices;
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

    struct DeviceQueue {
        std::deque<std::string> commands;
        std::set<std::string> scheduled;
        bool running{false};
        bool blocked{false};
        bool retry_wait{false};
        std::string repair_command;
        std::chrono::steady_clock::time_point retry_after;
    };

    /**
     * @brief 将命令 ID 放入设备 FIFO；重复提示合并，满队列留给补偿扫描。
     * @param[in] device_id 通信设备 ID；空串用于未路由命令。
     * @param[in] command_id 独立命令 ID。
     * @param[in] recovering 暂停期间恢复未决记录；工作线程等恢复受理后才处理。
     * @return 是否已排队或已有同一任务。
     */
    bool enqueue(const std::string& device_id, const std::string& command_id,
                 bool recovering = false);
    /** @brief 轮转设备队列，在固定大小工作池中并行受理；同设备仅一个受理任务。 */
    void worker_loop();
    /**
     * @brief 读取权威命令、校验设备归属，登记并调度或补同步结果。
     * @param[in] device_id 当前队列的设备 ID；通知路由不匹配时重新分发。
     * @param[in] command_id 独立命令 ID。
     * @return 本次受理或状态同步是否成功。
     */
    bool process(const std::string& device_id, const std::string& command_id);
    /**
     * @brief 从当前目录解析实际控制设备，不信任通知或调用方的设备标签。
     * @param[in] point_id 稳定控制点 ID。
     * @return 通信设备 ID；路由已移除时为空串。
     */
    std::string device_for(const std::string& point_id) const;

    struct WorkState {
        std::mutex mutex;
        std::condition_variable ready, drained;
        std::map<std::string, DeviceQueue> queues;
        std::deque<std::string> ready_devices;
        std::map<std::string, std::string> subscriptions, known_devices;
        std::size_t active_jobs{0};
        bool stopping{false};
    };

    std::shared_ptr<WorkState> work_{std::make_shared<WorkState>()};
    std::vector<std::thread> workers_;
    std::atomic<bool> worker_error_{false};

    // 北向控制先写实时库，再转交相同设备队列。
    std::mutex peer_mutex_;
    std::deque<PeerRequest> peer_queue_;
    std::string peer_prefix_;
};
}  // namespace ems::net::collect
