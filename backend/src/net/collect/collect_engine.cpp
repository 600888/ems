/**
 * @file collect_engine.cpp
 * @brief 采集引擎主类实现
 */
#include "collect_engine.h"

#include <spdlog/fmt/bundled/format.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>

#include "net/para_loader/para_loader.h"

namespace ems {
namespace net {
namespace collect {

using namespace ems::net::device;
using json = nlohmann::json;

// ============================================================
// 构造 / 析构
// ============================================================

CollectEngine::CollectEngine() = default;

CollectEngine::~CollectEngine() { shutdown(); }

// ============================================================
// 初始化
// ============================================================

bool CollectEngine::initialize(const EngineConfig& config) {
    if (state_.load() != EngineState::Idle) {
        LOG_WARN(
            fmt::format("采集引擎已被使用, 当前状态={}", static_cast<int>(state_.load())).c_str());
        return false;
    }

    LOG_INFO(fmt::format("初始化采集引擎: 插件目录={}, Redis={}:{}, 默认周期={}ms",
                         config.pluginDir_, config.redisHost_, config.redisPort_,
                         config.defaultPollIntervalMs_)
                 .c_str());
    config_ = config;

    // 1. 设置插件目录
    plugin_manager_.set_plugin_dir(config.pluginDir_);

    // 2. 初始化数据分发器
    DataDispatcherConfig dispatchConfig;
    dispatchConfig.redisHost_ = config.redisHost_;
    dispatchConfig.redisPort_ = config.redisPort_;
    dispatchConfig.redisPassword_ = config.redisPassword_;
    dispatchConfig.redisDb_ = config.redisDb_;
    dispatchConfig.poolSize_ = config.redisPoolSize_;
    dispatchConfig.timeoutMs_ = config.redisTimeoutMs_;
    dispatchConfig.keyPrefix_ = config.redisPrefix_;
    dispatchConfig.notificationBackend_ = config.notificationBackend_;
    dispatchConfig.zmqEndpoint_ = config.zmqEndpoint_;
    dispatchConfig.zmqQueueItems_ = config.zmqQueueItems_;
    dispatchConfig.zmqMaxMessageBytes_ = config.zmqMaxMessageBytes_;
    dispatchConfig.zmqHighWaterMark_ = config.zmqHighWaterMark_;

    dispatcher_ = std::make_shared<DataDispatcher>();
    if (!dispatcher_->initialize(dispatchConfig)) {
        LOG_ERROR(
            fmt::format("数据分发器初始化失败, Redis={}:{}", config.redisHost_, config.redisPort_)
                .c_str());
        dispatcher_.reset();
        return false;
    }

    LOG_INFO("采集引擎初始化成功");
    state_.store(EngineState::Idle);
    return true;
}

// ============================================================
// 启动所有通道
// ============================================================

int CollectEngine::start_all(ParaLoader& para_loader,
                             const std::vector<ems::db::point_yc>* yc_points,
                             const std::vector<ems::db::point_yx>* yx_points) {
    if (state_.load() == EngineState::Running) {
        LOG_WARN("采集引擎已在运行中");
        return 0;
    }

    if (!para_loader.is_initialized()) {
        LOG_ERROR("参数加载器未初始化");
        return -1;
    }

    // 1. 加载所有启用的通道配置
    auto channels = para_loader.load_channels();
    if (channels.empty()) {
        LOG_INFO("数据库中没有启用的通道");
        return 0;
    }

    if (channels.size() > static_cast<size_t>(config_.maxChannels_)) {
        LOG_WARN(
            fmt::format("通道数量{}超过最大值{}, 截断处理", channels.size(), config_.maxChannels_)
                .c_str());
        channels.resize(config_.maxChannels_);
    }

    LOG_INFO(fmt::format("从数据库加载了{}个通道配置", channels.size()).c_str());

    // 2. 加载测点配置（如果未提供）
    std::vector<ems::db::point_yc> localYcPoints;
    std::vector<ems::db::point_yx> localYxPoints;
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
        if (chConfig.status_ == 0) {
            LOG_INFO(fmt::format("通道[{}] {} 已禁用, 跳过", chConfig.id_, chConfig.name_).c_str());
            continue;
        }

        LOG_INFO(fmt::format("启动通道[{}] {}, 协议={}, 连接={}", chConfig.id_, chConfig.name_,
                             ems::rtdb::protocolTypeToString(chConfig.protocol_),
                             chConfig.connection_)
                     .c_str());

        // 构建设备上下文
        auto devices = build_device_contexts(chConfig, yc_points, yx_points);

        // 创建通道线程
        auto channelThread = std::make_shared<ChannelThread>(chConfig, dispatcher_);
        channelThread->set_devices(std::move(devices));
        channelThread->set_poll_interval(config_.defaultPollIntervalMs_);

        // 设置错误回调
        channelThread->set_error_callback(
            [this](int64_t chId, const std::string& err) { on_channel_error(chId, err); });

        // 加载协议插件
        std::string protocolName = ems::rtdb::protocolTypeToString(chConfig.protocol_);
        void* pluginInstance = plugin_manager_.load(protocolName);
        if (!pluginInstance) {
            LOG_ERROR(
                fmt::format("通道[{}] 加载插件失败: 协议={}", chConfig.id_, protocolName).c_str());
            on_channel_error(chConfig.id_, "Failed to load plugin for protocol: " + protocolName);
            continue;
        }

        // 注册到通道映射
        {
            std::lock_guard lock(channels_mutex_);
            channels_[chConfig.id_] = channelThread;
        }

        // 获取函数指针并启动通道线程
        auto fns = plugin_manager_.get_instance_functions(pluginInstance);
        channelThread->start(plugin_manager_, pluginInstance, fns);
        started++;
    }

    state_.store(EngineState::Running);
    LOG_INFO(fmt::format("采集引擎启动完成, 成功启动{}个通道", started).c_str());
    return started;
}

// ============================================================
// 启动/停止单个通道
// ============================================================

bool CollectEngine::start_channel(const ems::rtdb::ChannelConfig& channelConfig) {
    if (channelConfig.status_ == 0) return false;
    std::lock_guard lock(channels_mutex_);

    if (channels_.size() >= static_cast<size_t>(config_.maxChannels_)) {
        LOG_WARN(fmt::format("通道数量达到上限{}, 无法启动通道[{}]", config_.maxChannels_,
                             channelConfig.id_)
                     .c_str());
        return false;
    }

    // 检查是否已存在
    if (channels_.find(channelConfig.id_) != channels_.end()) {
        LOG_WARN(fmt::format("通道[{}] 已存在, 跳过", channelConfig.id_).c_str());
        return false;
    }

    LOG_INFO(fmt::format("启动单个通道[{}] {}, 协议={}, 连接={}", channelConfig.id_,
                         channelConfig.name_,
                         ems::rtdb::protocolTypeToString(channelConfig.protocol_),
                         channelConfig.connection_)
                 .c_str());

    auto channelThread = std::make_shared<ChannelThread>(channelConfig, dispatcher_);
    channelThread->set_poll_interval(config_.defaultPollIntervalMs_);

    channels_[channelConfig.id_] = channelThread;

    // 加载插件并启动（简化版，实际应通过 start_all 完整初始化）
    std::string protocolName = ems::rtdb::protocolTypeToString(channelConfig.protocol_);
    void* pluginInstance = plugin_manager_.load(protocolName);
    if (!pluginInstance) {
        LOG_ERROR(
            fmt::format("通道[{}] 加载插件失败: 协议={}", channelConfig.id_, protocolName).c_str());
        channels_.erase(channelConfig.id_);
        return false;
    }

    channelThread->start(plugin_manager_, pluginInstance,
                         plugin_manager_.get_instance_functions(pluginInstance));
    return true;
}

bool CollectEngine::stop_channel(int64_t channel_id) {
    LOG_INFO(fmt::format("停止单个通道[{}]", channel_id).c_str());
    std::shared_ptr<ChannelThread> channel;
    {
        std::lock_guard lock(channels_mutex_);
        auto it = channels_.find(channel_id);
        if (it == channels_.end()) {
            LOG_WARN(fmt::format("通道[{}] 不存在", channel_id).c_str());
            return false;
        }
        channel = it->second;
    }

    channel->stop();
    channel->join();

    {
        std::lock_guard lock(channels_mutex_);
        channels_.erase(channel_id);
    }

    LOG_INFO(fmt::format("通道[{}] 已停止并从管理中移除", channel_id).c_str());
    return true;
}

// ============================================================
// 停止所有通道 / 关闭引擎
// ============================================================

void CollectEngine::stop_all() {
    LOG_INFO(fmt::format("停止所有通道, 当前通道数={}", channels_.size()).c_str());
    std::vector<std::shared_ptr<ChannelThread>> allChannels;
    {
        std::lock_guard lock(channels_mutex_);
        for (auto& [_, ch] : channels_) {
            allChannels.push_back(ch);
        }
    }

    // 通知所有通道停止
    for (auto& ch : allChannels) {
        LOG_INFO(fmt::format("通知通道[{}] 停止", ch->channel_id()).c_str());
        ch->stop();
    }

    // 等待所有线程结束
    for (auto& ch : allChannels) {
        ch->join();
    }

    // 各通道工作线程退出时已按实例释放租约，不按协议名重复卸载。
    {
        std::lock_guard lock(channels_mutex_);
        channels_.clear();
    }

    state_.store(EngineState::Stopped);
    LOG_INFO("所有通道已停止");
}

void CollectEngine::shutdown() {
    LOG_INFO("关闭采集引擎");
    stop_all();
    if (dispatcher_) {
        dispatcher_->shutdown();
        dispatcher_.reset();
    }
    state_.store(EngineState::Idle);
    LOG_INFO("采集引擎已关闭");
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
                         [](const auto& pair) { return pair.second->is_running(); });
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

std::shared_ptr<ChannelThread> CollectEngine::find_channel(int64_t channel_id) const {
    std::lock_guard lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    return it != channels_.end() ? it->second : nullptr;
}

// ============================================================
// 错误回调
// ============================================================

void CollectEngine::on_channel_error(int64_t channel_id, const std::string& error) {
    LOG_ERROR(fmt::format("通道[{}] 错误: {}", channel_id, error).c_str());
    if (error_cb_) {
        error_cb_(channel_id, error);
    }
}

// ============================================================
// 构建设备上下文
// ============================================================

std::vector<DeviceContext> CollectEngine::build_device_contexts(
    const ems::rtdb::ChannelConfig& channelConfig, const std::vector<ems::db::point_yc>* yc_points,
    const std::vector<ems::db::point_yx>* yx_points) {
    std::unordered_map<int64_t, DeviceContext> deviceMap;

    // 处理 YC 测点
    if (yc_points) {
        for (const auto& pt : *yc_points) {
            if (pt.channel_id != channelConfig.id_) continue;

            auto& ctx = deviceMap[pt.device_id];
            ctx.deviceId_ = pt.device_id;
            ctx.deviceName_ = pt.name;
            ctx.ycPointCodes_.push_back(pt.code);
        }
    }

    // 处理 YX 测点
    if (yx_points) {
        for (const auto& pt : *yx_points) {
            if (pt.channel_id != channelConfig.id_) continue;

            auto& ctx = deviceMap[pt.device_id];
            ctx.deviceId_ = pt.device_id;
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

}  // namespace collect
}  // namespace net
}  // namespace ems
