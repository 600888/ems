/**
 * @file collect_engine.cpp
 * @brief 采集引擎主类实现
 */
#include "collect_engine.h"
#include "net/para_loader/para_loader.h"
#include <algorithm>
#include <sstream>
#include <nlohmann/json.hpp>

namespace ems {
namespace net {
namespace collect {

using namespace ems::net::device;
using json = nlohmann::json;

// ============================================================
// 构造 / 析构
// ============================================================

CollectEngine::CollectEngine() = default;

CollectEngine::~CollectEngine() {
    shutdown();
}

// ============================================================
// 初始化
// ============================================================

bool CollectEngine::initialize(const EngineConfig& config) {
    if (state_.load() != EngineState::Idle) {
        return false;
    }

    config_ = config;

    // 1. 设置插件目录
    plugin_manager_.set_plugin_dir(config.pluginDir_);

    // 2. 初始化数据分发器
    DataDispatcherConfig dispatchConfig;
    dispatchConfig.redisHost_     = config.redisHost_;
    dispatchConfig.redisPort_     = config.redisPort_;
    dispatchConfig.redisPassword_ = config.redisPassword_;
    dispatchConfig.redisDb_       = config.redisDb_;
    dispatchConfig.poolSize_      = config.redisPoolSize_;
    dispatchConfig.timeoutMs_     = config.redisTimeoutMs_;
    dispatchConfig.keyPrefix_     = config.redisPrefix_;

    dispatcher_ = std::make_shared<DataDispatcher>();
    if (!dispatcher_->initialize(dispatchConfig)) {
        dispatcher_.reset();
        return false;
    }

    state_.store(EngineState::Idle);
    return true;
}

// ============================================================
// 启动所有通道
// ============================================================

int CollectEngine::start_all(ParaLoader& para_loader,
                              const std::vector<ems::db::PointYcEntity>* yc_points,
                              const std::vector<ems::db::PointYxEntity>* yx_points) {
    if (state_.load() == EngineState::Running) {
        return 0;
    }

    if (!para_loader.is_initialized()) {
        return -1;
    }

    // 1. 加载所有启用的通道配置
    auto channels = para_loader.load_channels();
    if (channels.empty()) {
        return 0;
    }

    if (channels.size() > static_cast<size_t>(config_.maxChannels_)) {
        channels.resize(config_.maxChannels_);
    }

    // 2. 加载测点配置（如果未提供）
    std::vector<ems::db::PointYcEntity> localYcPoints;
    std::vector<ems::db::PointYxEntity> localYxPoints;
    if (!yc_points) {
        localYcPoints = para_loader.load_yc_points();
        yc_points = &localYcPoints;
    }
    if (!yx_points) {
        localYxPoints = para_loader.load_yx_points();
        yx_points = &localYxPoints;
    }

    // 3. 转换为运行时配置
    auto channelConfigs = para_loader.to_channel_configs(channels);

    // 4. 启动每个通道
    int started = 0;
    for (auto& chConfig : channelConfigs) {
        if (chConfig.status_ == 0) continue; // 跳过禁用的通道

        // 构建设备上下文
        auto devices = build_device_contexts(chConfig, yc_points, yx_points);

        // 创建通道线程
        auto channelThread = std::make_shared<ChannelThread>(chConfig, dispatcher_);
        channelThread->set_devices(std::move(devices));
        channelThread->set_poll_interval(config_.defaultPollIntervalMs_);

        // 设置错误回调
        channelThread->set_error_callback(
            [this](int64_t chId, const std::string& err) {
                on_channel_error(chId, err);
            });

        // 加载协议插件
        std::string protocolName = ems::rtdb::protocolTypeToString(chConfig.protocol_);
        void* pluginInstance = plugin_manager_.load(protocolName);
        if (!pluginInstance) {
            // 插件加载失败，记录错误继续下一个通道
            on_channel_error(chConfig.id_,
                "Failed to load plugin for protocol: " + protocolName);
            continue;
        }

        // 注册到通道映射
        {
            std::lock_guard lock(channels_mutex_);
            channels_[chConfig.id_] = channelThread;
        }

        // 获取函数指针并启动通道线程
        auto fns = plugin_manager_.get_functions(protocolName);
        channelThread->start(plugin_manager_, pluginInstance, fns);
        started++;
    }

    state_.store(EngineState::Running);
    return started;
}

// ============================================================
// 启动/停止单个通道
// ============================================================

bool CollectEngine::start_channel(const ems::rtdb::ChannelConfig& channelConfig) {
    std::lock_guard lock(channels_mutex_);

    if (channels_.size() >= static_cast<size_t>(config_.maxChannels_)) {
        return false;
    }

    // 检查是否已存在
    if (channels_.find(channelConfig.id_) != channels_.end()) {
        return false;
    }

    auto channelThread = std::make_shared<ChannelThread>(channelConfig, dispatcher_);
    channelThread->set_poll_interval(config_.defaultPollIntervalMs_);

    channels_[channelConfig.id_] = channelThread;

    // 加载插件并启动（简化版，实际应通过 start_all 完整初始化）
    std::string protocolName = ems::rtdb::protocolTypeToString(channelConfig.protocol_);
    void* pluginInstance = plugin_manager_.load(protocolName);
    if (!pluginInstance) {
        return false;
    }

    channelThread->start(plugin_manager_, pluginInstance,
                          PluginManager::PluginFunctions{});
    return true;
}

bool CollectEngine::stop_channel(int64_t channelId) {
    std::shared_ptr<ChannelThread> channel;
    {
        std::lock_guard lock(channels_mutex_);
        auto it = channels_.find(channelId);
        if (it == channels_.end()) return false;
        channel = it->second;
    }

    channel->stop();
    channel->join();

    // 卸载协议
    std::string protocolName = ems::rtdb::protocolTypeToString(
        channel->channel_config().protocol_);
    plugin_manager_.unload(protocolName);

    {
        std::lock_guard lock(channels_mutex_);
        channels_.erase(channelId);
    }

    return true;
}

// ============================================================
// 停止所有通道 / 关闭引擎
// ============================================================

void CollectEngine::stop_all() {
    std::vector<std::shared_ptr<ChannelThread>> allChannels;
    {
        std::lock_guard lock(channels_mutex_);
        for (auto& [_, ch] : channels_) {
            allChannels.push_back(ch);
        }
    }

    // 通知所有通道停止
    for (auto& ch : allChannels) {
        ch->stop();
    }

    // 等待所有线程结束
    for (auto& ch : allChannels) {
        ch->join();
    }

    // 卸载所有插件
    for (auto& name : plugin_manager_.loaded_protocols()) {
        plugin_manager_.unload(name);
    }

    {
        std::lock_guard lock(channels_mutex_);
        channels_.clear();
    }

    state_.store(EngineState::Stopped);
}

void CollectEngine::shutdown() {
    stop_all();
    if (dispatcher_) {
        dispatcher_->shutdown();
        dispatcher_.reset();
    }
    state_.store(EngineState::Idle);
}

// ============================================================
// 查询
// ============================================================

size_t CollectEngine::channel_count() const {
    std::lock_guard lock(channels_mutex_);
    return channels_.size();
}

size_t CollectEngine::running_count() const {
    std::lock_guard lock(channels_mutex_);
    return std::count_if(channels_.begin(), channels_.end(),
        [](const auto& pair) {
            return pair.second->is_running();
        });
}

std::vector<std::shared_ptr<ChannelThread>> CollectEngine::channels() const {
    std::lock_guard lock(channels_mutex_);
    std::vector<std::shared_ptr<ChannelThread>> result;
    result.reserve(channels_.size());
    for (const auto& [_, ch] : channels_) {
        result.push_back(ch);
    }
    return result;
}

std::shared_ptr<ChannelThread> CollectEngine::find_channel(int64_t channelId) const {
    std::lock_guard lock(channels_mutex_);
    auto it = channels_.find(channelId);
    return it != channels_.end() ? it->second : nullptr;
}

// ============================================================
// 错误回调
// ============================================================

void CollectEngine::on_channel_error(int64_t channelId, const std::string& error) {
    if (error_cb_) {
        error_cb_(channelId, error);
    }
}

// ============================================================
// 构建设备上下文
// ============================================================

std::vector<DeviceContext> CollectEngine::build_device_contexts(
    const ems::rtdb::ChannelConfig& channelConfig,
    const std::vector<ems::db::PointYcEntity>* yc_points,
    const std::vector<ems::db::PointYxEntity>* yx_points)
{
    std::unordered_map<int64_t, DeviceContext> deviceMap;

    // 处理 YC 测点
    if (yc_points) {
        for (const auto& pt : *yc_points) {
            if (pt.channel_id != channelConfig.id_) continue;

            auto& ctx = deviceMap[pt.device_id];
            ctx.deviceId_   = pt.device_id;
            ctx.deviceName_ = pt.name;
            ctx.ycPointCodes_.push_back(pt.code);
        }
    }

    // 处理 YX 测点
    if (yx_points) {
        for (const auto& pt : *yx_points) {
            if (pt.channel_id != channelConfig.id_) continue;

            auto& ctx = deviceMap[pt.device_id];
            ctx.deviceId_   = pt.device_id;
            ctx.deviceName_ = pt.name;
            ctx.yxPointCodes_.push_back(pt.code);
        }
    }

    std::vector<DeviceContext> result;
    result.reserve(deviceMap.size());
    for (auto& [_, ctx] : deviceMap) {
        result.push_back(std::move(ctx));
    }

    return result;
}

} // namespace collect
} // namespace net
} // namespace ems
