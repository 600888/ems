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

#include <common/database/configuration/channel.h>
#include <common/database/configuration/device.h>
#include <common/database/configuration/point.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "channel_thread.h"
#include "data_dispatcher.h"
#include "net/collect/collect_defaults.h"
#include "net/collect/collect_types.h"
#include "plugin_manager.h"
#include "rtdb/rtdb_device.h"

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
    std::string pluginDir_{"./lib"};  // 插件动态库目录

    // Redis/RTDB 配置
    std::string redisHost_{"127.0.0.1"};
    int redisPort_{enum_value(DispatchDefaults::RedisPort)};
    std::string redisPassword_;
    int redisDb_{0};
    int redisPoolSize_{enum_value(DispatchDefaults::RedisPoolSize)};
    int redisTimeoutMs_{enum_value(DispatchDefaults::RedisTimeoutMs)};
    std::string redisPrefix_{"ems:"};

    // 变更提示通道独立于 RTDB 存储选择。
    DataDispatcherConfig::NotificationBackend notificationBackend_{
        DataDispatcherConfig::NotificationBackend::Redis};
    std::string zmqEndpoint_{"tcp://127.0.0.1:5557"};
    std::size_t zmqQueueItems_{enum_value(DispatchDefaults::ZmqQueueItems)};
    std::size_t zmqMaxMessageBytes_{enum_value(DispatchDefaults::ZmqMaxMessageBytes)};
    int zmqHighWaterMark_{enum_value(DispatchDefaults::ZmqHighWaterMark)};

    // 默认采集周期
    int defaultPollIntervalMs_{enum_value(CollectDefaults::PollIntervalMs)};

    // 最大通道数
    int maxChannels_{enum_value(CollectDefaults::MaxChannels)};
};

/**
 * @brief 采集引擎主类
 *
 * 线程安全：内部持有互斥锁保护通道列表。
 */
class CollectEngine {
   public:
    /** @brief 构造 CollectEngine 实例。 */
    CollectEngine();
    /** @brief 释放 CollectEngine 持有的资源。 */
    ~CollectEngine();

    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    CollectEngine(const CollectEngine& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     */
    CollectEngine& operator=(const CollectEngine& other) = delete;
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    CollectEngine(CollectEngine&& other) noexcept = delete;
    /**
     * @brief 禁止复制或移动赋值。
     * @param[in] other 待赋值的源对象。
     */
    CollectEngine& operator=(CollectEngine&& other) noexcept = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    /**
     * @brief 初始化引擎
     * @param[in] config 引擎配置
     * @return true 成功
     */
    bool initialize(const EngineConfig& config);

    /**
     * @brief 启动所有已配置的通道
     *
     * 从 ParaLoader 加载通道配置，逐一创建并启动通道线程。
     *
     * @param[in] para_loader 参数加载器引用
     * @param[in] yc_points   遥测测点配置（可选，用于构建设备上下文）
     * @param[in] yx_points   遥信测点配置（可选）
     * @return 成功启动的通道数
     */
    int start_all(device::ParaLoader& para_loader,
                  const std::vector<ems::database::AnalogPoint>* yc_points = nullptr,
                  const std::vector<ems::database::StatusPoint>* yx_points = nullptr);

    /**
     * @brief 启动单个通道
     * @param[in] channelConfig 通道配置
     * @return true 成功
     */
    bool start_channel(const ems::rtdb::ChannelConfig& channelConfig);

    /**
     * @brief 停止所有通道
     */
    void stop_all();

    /**
     * @brief 停止单个通道
     * @param[in] channel_id 通道 ID
     * @return true 成功
     */
    bool stop_channel(int64_t channel_id);

    /**
     * @brief 关闭引擎并释放资源
     */
    void shutdown();

    // ============================================================
    // 查询
    // ============================================================

    /**
     * @brief 获取引擎状态
     * @return 得到的采集引擎状态。
     */
    EngineState state() const { return state_.load(); }

    /**
     * @brief 获取通道数量
     * @return 处理或统计得到的数据项数量。
     */
    size_t channel_count() const;

    /**
     * @brief 获取运行中的通道数量
     * @return 处理或统计得到的数据项数量。
     */
    size_t running_count() const;

    /**
     * @brief 获取所有通道线程列表
     * @return 对应对象的指针。
     */
    std::vector<std::shared_ptr<ChannelThread>> channels() const;

    /**
     * @brief 按 ID 查找通道
     * @param[in] channel_id 通道 ID。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<ChannelThread> find_channel(int64_t channel_id) const;

    /**
     * @brief 是否正在运行
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_running() const { return state_.load() == EngineState::Running; }

    // ============================================================
    // 组件访问
    // ============================================================

    /**
     * @brief 获取插件管理器引用
     * @return 对应对象的引用。
     */
    PluginManager& plugin_manager() { return plugin_manager_; }

    /**
     * @brief 获取数据分发器引用
     * @return 对应对象的指针。
     */
    std::shared_ptr<DataDispatcher> data_dispatcher() { return dispatcher_; }

    // ============================================================
    // 事件回调
    // ============================================================

    /** @brief 通道状态变更回调 */
    using ChannelStateCallback =
        std::function<void(int64_t channel_id, const std::string& channelName,
                           ChannelState oldState, ChannelState newState)>;

    /** @brief 采集错误回调 */
    using ErrorCallback = std::function<void(int64_t channel_id, const std::string& error)>;

    /**
     * @brief 设置通道状态变化回调。
     * @param[in] cb 操作完成或变化时调用的回调。
     */
    void set_channel_state_callback(ChannelStateCallback cb) { state_cb_ = std::move(cb); }

    /**
     * @brief 设置错误回调。
     * @param[in] cb 操作完成或变化时调用的回调。
     */
    void set_error_callback(ErrorCallback cb) { error_cb_ = std::move(cb); }

   private:
    /**
     * @brief 创建设备上下文列表
     * @param[in] channelConfig 通道配置。
     * @param[in] yc_points 遥测测点列表。
     * @param[in] yx_points 遥信测点列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<DeviceContext> build_device_contexts(
        const ems::rtdb::ChannelConfig& channelConfig,
        const std::vector<ems::database::AnalogPoint>* yc_points,
        const std::vector<ems::database::StatusPoint>* yx_points);

    /**
     * @brief 处理通道上报的错误。
     * @param[in] channel_id 通道 ID。
     * @param[in] error 错误说明。
     */
    void on_channel_error(int64_t channel_id, const std::string& error);

    EngineConfig config_;
    std::atomic<EngineState> state_{EngineState::Idle};
    PluginManager plugin_manager_;

    // 通道管理
    mutable std::mutex channels_mutex_;
    std::unordered_map<int64_t, std::shared_ptr<ChannelThread>> channels_;

    // 数据分发器（所有通道线程共享）
    std::shared_ptr<DataDispatcher> dispatcher_;

    // 回调
    ChannelStateCallback state_cb_;
    ErrorCallback error_cb_;
};

}  // namespace collect
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_COLLECT_COLLECT_ENGINE_H
