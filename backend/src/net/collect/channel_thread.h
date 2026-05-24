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

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <cstdint>

#include "net/protocol/common/protocol_plugin_api.h"
#include "net/log.h"
#include "rtdb/rtdb_channel.h"
#include "data_dispatcher.h"
#include "plugin_manager.h"

namespace ems {
namespace net {
namespace collect {

using rtdb::ChannelConfig;

/**
 * @brief 设备上下文（通道路径下的设备及其测点列表）
 */
struct DeviceContext {
    int64_t     deviceId_{0};
    std::string deviceName_;
    std::vector<std::string> ycPointCodes_;   // 遥测测点编码列表
    std::vector<std::string> yxPointCodes_;   // 遥信测点编码列表
    std::vector<std::string> ykPointCodes_;   // 遥控测点编码列表
    std::vector<std::string> ytPointCodes_;   // 遥调测点编码列表
};

/**
 * @brief 命令任务（遥控/遥调下发）
 */
struct CommandTask {
    int64_t     taskId_{0};
    std::string pointCode_;
    double      value_{0.0};
    int         timeoutMs_{5000};
    std::function<void(bool)> callback_;
};

/**
 * @brief 通道状态
 */
enum class ChannelState : int {
    Created  = 0,
    Starting = 1,
    Running  = 2,
    Stopping = 3,
    Stopped  = 4,
    Error    = 5
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
     * @param channelConfig 通道配置
     * @param dispatcher    数据分发器（共享引用）
     */
    explicit ChannelThread(const ChannelConfig& channelConfig,
                            std::shared_ptr<DataDispatcher> dispatcher);
    ~ChannelThread();

    ChannelThread(const ChannelThread&) = delete;
    ChannelThread& operator=(const ChannelThread&) = delete;
    ChannelThread(ChannelThread&&) noexcept = delete;
    ChannelThread& operator=(ChannelThread&&) noexcept = delete;

    // ============================================================
    // 生命周期管理
    // ============================================================

    /**
     * @brief 启动通道线程
     *
     * 创建 std::thread 并进入采集循环。
     * 根据协议名称通过 PluginManager 加载对应插件。
     *
     * @param plugin_manager 插件管理器
     * @param plugin_instance 协议插件实例（由 PluginManager::load 创建）
     * @param plugin_fns 插件函数指针结构
     */
    void start(PluginManager& plugin_manager, void* plugin_instance,
               const PluginManager::PluginFunctions& plugin_fns);

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
     */
    ChannelState state() const { return state_.load(); }

    // ============================================================
    // 配置
    // ============================================================

    /**
     * @brief 设置采集周期
     * @param ms 毫秒
     */
    void set_poll_interval(int ms);

    /**
     * @brief 获取采集周期
     */
    int poll_interval() const { return poll_interval_ms_.load(); }

    /**
     * @brief 添加设备上下文
     */
    void add_device(DeviceContext device);

    /**
     * @brief 设置设备列表
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
     * @param pointCode 测点编码
     * @param value     控制值
     * @param timeoutMs 超时时间
     * @return 异步结果
     */
    std::future<bool> post_command(const std::string& pointCode,
                                    double value, int timeoutMs = 5000);

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取通道配置
     */
    const ChannelConfig& channel_config() const { return channel_config_; }

    /**
     * @brief 获取通道编码（用于日志标识）
     */
    const std::string& chanCode() const { return channel_config_.name_; }

    /**
     * @brief 获取通道 ID
     */
    int64_t channel_id() const { return channel_config_.id_; }

    /**
     * @brief 获取通道名称
     */
    const std::string& channel_name() const { return channel_config_.name_; }

    /**
     * @brief 获取设备列表
     */
    std::vector<DeviceContext> devices() const;

    /**
     * @brief 获取重连统计
     */
    int reconnect_count() const { return reconnect_count_.load(); }

    /**
     * @brief 是否正在运行
     */
    bool is_running() const { return state_.load() == ChannelState::Running; }

    /**
     * @brief 获取最近错误信息
     */
    std::string last_error() const { return last_err_; }

    // ============================================================
    // 错误处理回调
    // ============================================================

    using ErrorCallback = std::function<void(int64_t channel_id,
                                             const std::string& error)>;
    void set_error_callback(ErrorCallback cb) { error_cb_ = std::move(cb); }

private:
    // ---- 线程主循环 ----
    void run(PluginManager& plugin_manager);

    // ---- 采集步骤 ----
    bool check_connection();
    bool reconnect();
    void process_command_queue();
    void collect_device(const DeviceContext& device);
    void collect_points(const std::vector<std::string>& pointCodes);

    // ---- 状态切换 ----
    void set_state(ChannelState new_state);

    // ---- 重连退避 ----
    int  backoff_delay() const;

    const ChannelConfig             channel_config_;
    std::shared_ptr<DataDispatcher> dispatcher_;
    std::shared_ptr<std::thread>    thread_;

    // 插件接口（通过 C 接口操作）
    void*                               plugin_instance_{nullptr};
    PluginManager::PluginFunctions      plugin_fns_;

    // 设备列表
    std::vector<DeviceContext>           devices_;
    mutable std::mutex                   device_mutex_;

    // 命令队列
    std::deque<CommandTask>              cmd_queue_;
    mutable std::mutex                   cmd_mutex_;
    std::condition_variable              cmd_cv_;

    // 状态控制
    std::atomic<ChannelState>            state_{ChannelState::Created};
    std::atomic<int>                     poll_interval_ms_{1000};
    std::atomic<int>                     reconnect_count_{0};
    int                                  max_retries_{5};

    // 错误处理
    mutable std::string                  last_err_;
    ErrorCallback                        error_cb_;
};

} // namespace collect
} // namespace net
} // namespace ems

#endif // EMS_NET_COLLECT_CHANNEL_THREAD_H
