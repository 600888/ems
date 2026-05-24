/**
 * @file collect_engine.h
 * @brief 采集引擎主类
 *
 * 采集进程的核心，管理所有通道线程的创建/销毁/启停，
 * 协调 PluginManager 的插件加载与 DataDispatcher 的数据分发。
 *
 * 启动流程:
 *   1. 初始化 PluginManager (设置插件目录)
 *   2. 初始化 DataDispatcher (连接 Redis)
 *   3. ParaLoader 加载通道/设备/测点配置
 *   4. start_all() 遍历通道，加载插件，创建并启动通道线程
 *
 * 停止流程:
 *   1. stop_all() 通知所有通道线程优雅退出
 *   2. 等待所有线程 join
 *   3. 卸载所有插件
 *   4. 关闭 DataDispatcher
 */
#ifndef EMS_NET_COLLECT_COLLECT_ENGINE_H
#define EMS_NET_COLLECT_COLLECT_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <functional>

#include "rtdb/rtdb_device.h"
#include "db/device_entity.h"
#include "db/channel_entity.h"
#include "db/point_entity.h"
#include "plugin_manager.h"
#include "channel_thread.h"
#include "data_dispatcher.h"

namespace ems {
namespace net {
namespace device {
class ParaLoader;
}  // namespace device

namespace collect {

/**
 * @brief 采集引擎配置
 */
struct EngineConfig {
    std::string pluginDir_{"./lib"};           // 插件动态库目录

    // Redis/RTDB 配置
    std::string redisHost_{"127.0.0.1"};
    int         redisPort_{6379};
    std::string redisPassword_;
    int         redisDb_{0};
    int         redisPoolSize_{4};
    int         redisTimeoutMs_{3000};
    std::string redisPrefix_{"ems:"};

    // 默认采集周期
    int defaultPollIntervalMs_{1000};

    // 最大通道数
    int maxChannels_{128};
};

/**
 * @brief 采集引擎状态
 */
enum class EngineState : int {
    Idle    = 0,
    Running = 1,
    Stopped = 2,
    Error   = 3
};

/**
 * @brief 采集引擎主类
 *
 * 线程安全：内部持有互斥锁保护通道列表。
 */
class CollectEngine {
public:
    CollectEngine();
    ~CollectEngine();

    CollectEngine(const CollectEngine&) = delete;
    CollectEngine& operator=(const CollectEngine&) = delete;
    CollectEngine(CollectEngine&&) noexcept = delete;
    CollectEngine& operator=(CollectEngine&&) noexcept = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    /**
     * @brief 初始化引擎
     * @param config 引擎配置
     * @return true 成功
     */
    bool initialize(const EngineConfig& config);

    /**
     * @brief 启动所有已配置的通道
     *
     * 从 ParaLoader 加载通道配置，逐一创建并启动通道线程。
     *
     * @param para_loader 参数加载器引用
     * @param yc_points   遥测测点配置（可选，用于构建设备上下文）
     * @param yx_points   遥信测点配置（可选）
     * @return 成功启动的通道数
     */
    int start_all(device::ParaLoader& para_loader,
                  const std::vector<ems::db::PointYcEntity>* yc_points = nullptr,
                  const std::vector<ems::db::PointYxEntity>* yx_points = nullptr);

    /**
     * @brief 启动单个通道
     * @param channelConfig 通道配置
     * @return true 成功
     */
    bool start_channel(const ems::rtdb::ChannelConfig& channelConfig);

    /**
     * @brief 停止所有通道
     */
    void stop_all();

    /**
     * @brief 停止单个通道
     * @param channelId 通道 ID
     * @return true 成功
     */
    bool stop_channel(int64_t channelId);

    /**
     * @brief 关闭引擎并释放资源
     */
    void shutdown();

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取引擎状态
     */
    EngineState state() const { return state_.load(); }

    /**
     * @brief 获取通道数量
     */
    size_t channel_count() const;

    /**
     * @brief 获取运行中的通道数量
     */
    size_t running_count() const;

    /**
     * @brief 获取所有通道线程列表
     */
    std::vector<std::shared_ptr<ChannelThread>> channels() const;

    /**
     * @brief 按 ID 查找通道
     */
    std::shared_ptr<ChannelThread> find_channel(int64_t channelId) const;

    /**
     * @brief 是否正在运行
     */
    bool is_running() const { return state_.load() == EngineState::Running; }

    // ============================================================
    // 组件访问
    // ============================================================

    /**
     * @brief 获取插件管理器引用
     */
    PluginManager& plugin_manager() { return plugin_manager_; }

    /**
     * @brief 获取数据分发器引用
     */
    std::shared_ptr<DataDispatcher> data_dispatcher() { return dispatcher_; }

    // ============================================================
    // 事件回调
    // ============================================================

    /** @brief 通道状态变更回调 */
    using ChannelStateCallback = std::function<void(
        int64_t channelId, const std::string& channelName,
        ChannelState oldState, ChannelState newState)>;

    /** @brief 采集错误回调 */
    using ErrorCallback = std::function<void(
        int64_t channelId, const std::string& error)>;

    void set_channel_state_callback(ChannelStateCallback cb) {
        state_cb_ = std::move(cb);
    }

    void set_error_callback(ErrorCallback cb) {
        error_cb_ = std::move(cb);
    }

private:
    /**
     * @brief 创建设备上下文列表
     */
    std::vector<DeviceContext> build_device_contexts(
        const ems::rtdb::ChannelConfig& channelConfig,
        const std::vector<ems::db::PointYcEntity>* yc_points,
        const std::vector<ems::db::PointYxEntity>* yx_points);

    void on_channel_error(int64_t channelId, const std::string& error);

    EngineConfig                      config_;
    std::atomic<EngineState>          state_{EngineState::Idle};
    PluginManager                     plugin_manager_;

    // 通道管理
    mutable std::mutex                channels_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<ChannelThread>> channels_;

    // 数据分发器（所有通道线程共享）
    std::shared_ptr<DataDispatcher>   dispatcher_;

    // 回调
    ChannelStateCallback              state_cb_;
    ErrorCallback                     error_cb_;
};

} // namespace collect
} // namespace net
} // namespace ems

#endif // EMS_NET_COLLECT_COLLECT_ENGINE_H
