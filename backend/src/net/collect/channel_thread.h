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
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data_dispatcher.h"
#include "net/collect/collect_defaults.h"
#include "net/collect/collect_types.h"
#include "net/handler/protocol_handler.h"
#include "net/log.h"
#include "net/para_loader/cfg_loader.h"
#include "plugin_manager.h"
#include "rtdb/model/rtdb_channel.h"

namespace ems {
namespace net {
namespace collect {

using rtdb::ChannelConfig;

/**
 * @brief 设备上下文（通道路径下的设备及其测点列表）
 */
using DeviceContext = device::ConfiguredDevice;

/**
 * @brief 命令任务（遥控/遥调下发）
 */
struct CommandTask {
    int64_t taskId_{0};
    std::string pointCode_;
    double value_{0.0};
    int timeoutMs_{enum_value(CollectDefaults::CommandTimeoutMs)};
    std::function<void(bool)> callback_;
};

/**
 * @brief 通道线程
 *
 * 每个 ChannelThread 对象对应一个独立线程，
 * 通过 start() 启动线程，stop() 停止线程。
 */
class ChannelThread {
   public:
    /**
     * @param[in] channelConfig 通道配置
     * @param[in] dispatcher    数据分发器（共享引用）
     */
    explicit ChannelThread(const ChannelConfig& channelConfig,
                           std::shared_ptr<DataDispatcher> dispatcher);
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

    // ============================================================
    // 命令下发
    // ============================================================

    /**
     * @brief 下发遥控/遥调命令
     *
     * 命令进入队列，在采集循环中优先处理。
     * 通过返回的 std::future<bool> 获取执行结果。
     *
     * @param[in] pointCode 测点编码
     * @param[in] value     控制值
     * @param[in] timeoutMs 超时时间
     * @return 异步结果
     */
    std::future<bool> post_command(const std::string& pointCode, double value,
                                   int timeoutMs = enum_value(CollectDefaults::CommandTimeoutMs));

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取通道配置
     * @return 对应对象的引用。
     */
    const ChannelConfig& channel_config() const { return channel_config_; }

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
    /** @brief 处理待下发的控制命令。 */
    void process_command_queue();
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

    // ---- 状态切换 ----
    /**
     * @brief 更新通道运行状态。
     * @param[in] new_state 目标通道状态。
     */
    void set_state(ChannelState new_state);

    // ---- 重连退避 ----
    /**
     * @brief 计算下次重连的退避时间。
     * @return 计算得到的数值。
     */
    int backoff_delay() const;

    const ChannelConfig channel_config_;
    std::shared_ptr<DataDispatcher> dispatcher_;
    std::shared_ptr<std::thread> thread_;

    // 处理器拥有插件实例；工作线程退出后才释放库租约。
    std::unique_ptr<handler::ProtocolHandler> handler_;

    // 设备列表
    std::vector<DeviceContext> devices_;
    mutable std::mutex device_mutex_;

    // 命令队列
    std::deque<CommandTask> cmd_queue_;
    mutable std::mutex cmd_mutex_;
    std::condition_variable cmd_cv_;

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
