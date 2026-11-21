/**
 * @file channel_thread.h
 * @brief 通道线程
 *
 * 每个通道对应一个独立线程 (std::thread)，持有该通道的协议插件实例，
 * 执行周期性采集循环：检查连接 -> 处理命令队列 -> 批量读取测点 -> 数据分发。
 *
 * 状态机: Created -> Starting -> Running -> Stopping -> Stopped
 *                \-> Error (初始化/连接失败)
 *
 * 一个通道一个线程的原则保证了故障隔离和协议隔离。
 */
#ifndef EMS_NET_COLLECT_CHANNEL_THREAD_H
#define EMS_NET_COLLECT_CHANNEL_THREAD_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "command_types.h"
#include "data_dispatcher.h"
#include "net/channel_config.h"
#include "net/collect/collect_defaults.h"
#include "net/collect/collect_types.h"
#include "net/handler/protocol_handler.h"
#include "net/log.h"
#include "net/para_loader/cfg_loader.h"
#include "plugin_manager.h"

namespace ems {
namespace net {
namespace collect {

using ems::net::ChannelConfig;

/**
 * @brief 设备上下文（通道路径下的设备及其测点列表）
 */
using DeviceContext = device::ConfiguredDevice;

/**
 * @brief 命令任务（遥控/遥调下发）
 */
struct CommandTask {
    PointCommand command;
    std::chrono::steady_clock::time_point deadline;
    BeforeCommand before_send;
    CommandCompletion completion;
};

/**
 * @brief 通道线程
 *
 * 每个 ChannelThread 对象对应一个独立线程，
 * 通过 start() 启动线程，stop() 停止线程。
 */
class ChannelThread {
   public:
    using HandlerFactory = std::function<std::unique_ptr<handler::ProtocolHandler>(PluginManager&)>;
    /**
     * @param[in] channelConfig 通道配置
     * @param[in] dispatcher    数据分发器（共享引用）
     * @param[in] factory 可选处理器工厂；为空时使用正式插件工厂。
     */
    explicit ChannelThread(const ChannelConfig& channelConfig,
                           std::shared_ptr<DataDispatcher> dispatcher, HandlerFactory factory = {});
    /** @brief 释放 ChannelThread 持有的资源。 */
    ~ChannelThread();

    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    ChannelThread(const ChannelThread& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     */
    ChannelThread& operator=(const ChannelThread& other) = delete;
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    ChannelThread(ChannelThread&& other) noexcept = delete;
    /**
     * @brief 禁止复制或移动赋值。
     * @param[in] other 待赋值的源对象。
     */
    ChannelThread& operator=(ChannelThread&& other) noexcept = delete;

    // ============================================================
    // 生命周期管理
    // ============================================================

    /**
     * @brief 创建并初始化统一处理器，然后启动通道采集线程。
     * @param[in,out] plugin_manager 共享管理器，须在通道停止并 join 后才销毁。
     * @return true 线程启动成功；false 状态不允许或处理器初始化失败。
     */
    bool start(PluginManager& plugin_manager);

    /**
     * @brief 请求停止通道线程（优雅退出）
     */
    void stop();

    /**
     * @brief 等待线程结束
     */
    void join();

    /**
     * @brief 获取通道状态
     * @return 得到的通道状态。
     */
    ChannelState state() const { return state_.load(); }

    // ============================================================
    // 配置
    // ============================================================

    /**
     * @brief 设置采集周期
     * @param[in] ms 毫秒
     */
    void set_poll_interval(int ms);

    /**
     * @brief 获取采集周期
     * @return 计算得到的数值。
     */
    int poll_interval() const { return poll_interval_ms_.load(); }

    /**
     * @brief 添加设备上下文
     * @param[in] device 设备对象。
     */
    void add_device(DeviceContext device);

    /**
     * @brief 设置设备列表
     * @param[in] devices 设备列表。
     */
    void set_devices(std::vector<DeviceContext> devices);

    /**
     * @brief 从共享连接中移除逻辑通道的设备，后续周期不再调度；在途采集可能完成。
     * @param[in] channel_id 要停止的逻辑通道 ID。
     */
    void remove_devices(int64_t channel_id);

    // ============================================================
    // 命令下发
    // ============================================================

    /**
     * @brief 查找当前可控点，返回路由副本。
     * @param[in] point_id 稳定测点 ID。
     * @return 路由副本或空值。
     */
    std::optional<device::PointRoute> command_route(const std::string& point_id) const;

    /**
     * @brief 解析控制点绑定的通信设备，用于设备队列；部件点归到通信设备。
     * @param[in] point_id 稳定控制点 ID。
     * @return 通信设备 ID；路由不存在返回空串。
     */
    std::string command_device(const std::string& point_id) const;

    // ============================================================
    // 北向控制回传
    // ============================================================

    /** @brief 服务端受理的北向控制；point_id 由协议配置给出。 */
    struct PeerControl {
        std::int64_t control_id{0};
        std::string point_id;
        std::string code;
        double value{0};
        std::int64_t expires_at_ms{0};
    };

    /** @brief 管线回执；可在任意线程调用，协议回帧由通道线程下一周期发送。 */
    using PeerOutcomeHandler = std::function<void(bool accepted, const std::string& reason)>;
    /** @brief 控制转交入口；未设置时不从服务端取控制请求。 */
    using PeerControlSink = std::function<void(const PeerControl&, PeerOutcomeHandler)>;

    /**
     * @brief 设置北向控制转交入口。
     * @param[in] sink 由上层注入的命令受理函数；为空表示不启用控制回传。
     */
    void set_peer_control_sink(PeerControlSink sink);

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取通道配置
     * @return 对应对象的引用。
     */
    const ChannelConfig& channel_config() const { return channel_config_; }

    /**
     * @brief 登记共享物理连接中的逻辑通道，用于独立状态同步。
     * @param[in] id 逻辑通道数字主键。
     * @param[in] channel_id 稳定通道 ID。
     */
    void add_logical_channel(std::int64_t id, const std::string& channel_id);

    /**
     * @brief 获取通道编码（用于日志标识）
     * @return 对应对象的引用。
     */
    const std::string& chanCode() const { return channel_config_.name_; }

    /**
     * @brief 获取通道 ID
     * @return 计算得到的数值。
     */
    int64_t channel_id() const { return channel_config_.id_; }

    /**
     * @brief 获取通道名称
     * @return 对应对象的引用。
     */
    const std::string& channel_name() const { return channel_config_.name_; }

    /**
     * @brief 获取设备列表
     * @return 匹配条件的数据集合。
     */
    std::vector<DeviceContext> devices() const;

    /**
     * @brief 获取重连统计
     * @return 处理或统计得到的数据项数量。
     */
    int reconnect_count() const { return reconnect_count_.load(); }

    /**
     * @brief 是否正在运行
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_running() const { return state_.load() == ChannelState::Running; }

    /**
     * @brief 获取最近错误信息
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const { return last_err_; }

    // ============================================================
    // 错误处理回调
    // ============================================================

    using ErrorCallback = std::function<void(int64_t channel_id, const std::string& error)>;

    /**
     * @brief 设置错误回调。
     * @param[in] cb 操作完成或变化时调用的回调。
     */
    void set_error_callback(ErrorCallback cb) { error_cb_ = std::move(cb); }

   private:
    friend class CommandService;
    /**
     * @brief 有界入队，唤醒空闲线程；不直接调用协议，停止后拒绝入队。
     * @param[in] command 稳定命令及截止时间。
     * @param[in] before_send 物理写入前的持久条件登记；失败不发送。
     * @param[in] completion 恰好一次的完成回调；入队失败时不调用。
     * @return true 已入队；false 队满、无回调或线程已停止。
     */
    bool enqueue_command(PointCommand command, BeforeCommand before_send,
                         CommandCompletion completion);

    // ---- 线程主循环 ----
    /** @brief 运行采集循环，并在全部退出路径销毁处理器。 */
    void run();

    // ---- 采集步骤 ----
    /**
     * @brief 检查通道连接状态。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool check_connection();
    /**
     * @brief 重新建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool reconnect();
    /** @brief 每个调度点最多执行一个命令，使采集与其他控制获得执行机会。 */
    void process_command_queue();
    /**
     * @brief 在通道线程执行单个任务，异常及发送不确定性返回 Unknown。
     * @param[in] task 当前命令。
     * @return 执行状态和诊断。
     */
    PointCommandResult execute_command(const CommandTask& task);
    /**
     * @brief 只读核验当前连接上的新反馈，绝不重发原命令。
     * @param[in] command 原命令身份、目标值及发送时间。
     * @param[in] route 命令路由及反馈规则。
     * @return Succeeded 或 Unknown。
     */
    PointCommandResult verify_command(const PointCommand& command, const device::PointRoute& route);
    /** @brief 收尾全部未发送任务，保证 future 不悬挂。 */
    void cancel_commands();
    /**
     * @brief 采集指定设备的测点。
     * @param[in] device 设备对象。
     */
    void collect_device(const DeviceContext& device);
    /**
     * @brief 批量采集指定测点。
     * @param[in] routes 稳定测点 ID 与协议地址路由。
     * @param[in] point_type 四遥类型 yc 或 yx。
     */
    void collect_points(const std::vector<device::PointRoute>& routes, const char* point_type);

    /** @brief 取出服务端插件已受理的北向控制，转交上层命令管线。 */
    void relay_peer_controls();
    /** @brief 把上层回执交回协议，向上级发送结束确认。 */
    void flush_peer_outcomes();

    // ---- 状态切换 ----
    /**
     * @brief 更新通道运行状态。
     * @param[in] new_state 目标通道状态。
     */
    void set_state(ChannelState new_state);

    /** @brief 发布当前连接状态，不修改生命周期，避免周期刷新覆盖停机请求。 */
    void publish_state();

    // ---- 重连退避 ----
    /**
     * @brief 计算下次重连的退避时间。
     * @return 计算得到的数值。
     */
    int backoff_delay() const;

    const ChannelConfig channel_config_;
    std::map<std::int64_t, std::string> logical_channels_;
    std::shared_ptr<DataDispatcher> dispatcher_;
    std::shared_ptr<std::thread> thread_;

    // 处理器拥有插件实例；工作线程退出后才释放库租约。
    std::unique_ptr<handler::ProtocolHandler> handler_;
    HandlerFactory handler_factory_;

    // 设备列表
    std::vector<DeviceContext> devices_;
    mutable std::mutex device_mutex_;

    // 命令队列
    std::map<std::string, std::deque<CommandTask>> device_commands_;
    std::deque<std::string> ready_devices_;
    std::size_t queued_commands_{0};
    mutable std::mutex cmd_mutex_;
    std::condition_variable cmd_cv_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_command_;

    // 北向控制回传：回执可能来自其他通道线程，统一在下一周期交回协议。
    struct PeerState {
        std::mutex mutex;
        std::deque<std::pair<std::int64_t, std::pair<bool, std::string>>> outcomes;
        std::atomic<bool> alive{true};
    };

    std::shared_ptr<PeerState> peer_state_{std::make_shared<PeerState>()};
    mutable std::mutex peer_sink_mutex_;
    PeerControlSink peer_sink_;
    bool relay_peer_controls_{false};  ///< 仅服务端角色轮询北向控制，避免客户端空转。

    // 状态控制
    std::atomic<ChannelState> state_{ChannelState::Created};
    std::atomic<int> poll_interval_ms_{enum_value(CollectDefaults::PollIntervalMs)};
    std::atomic<int> reconnect_count_{0};
    int max_retries_{enum_value(CollectDefaults::MaxRetries)};

    // 错误处理
    mutable std::string last_err_;
    ErrorCallback error_cb_;
};

}  // namespace collect
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_COLLECT_CHANNEL_THREAD_H
