/**
 * @file collect_engine.cpp
 * @brief 采集引擎主类实现
 */
#include "collect_engine.h"

#include <spdlog/fmt/bundled/format.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

#include "net/para_loader/cfg_loader.h"
#include "net/protocol/protocol_types.h"
#include "rtdb/client/rtdb_manager.h"

namespace ems {
namespace net {
namespace collect {

using namespace ems::net::device;
using json = nlohmann::json;

namespace {
json settings_of(const ems::rtdb::ChannelConfig& channel) {
    auto settings = channel.settings_.empty() ? json::object() : json::parse(channel.settings_);
    if (!settings.is_object()) throw std::invalid_argument("channel settings must be an object");
    return settings;
}

ems::rtdb::ChannelConfig connection_config(ems::rtdb::ChannelConfig channel,
                                           const std::vector<ConfiguredDevice>& devices) {
    auto settings = settings_of(channel);
    if (channel.protocol_ == ems::model::ProtocolType::IEC104 &&
        settings.value("local_role", settings.value("role", std::string("client"))) == "client") {
        std::set<int> addresses;
        if (settings.contains("common_addresses") || settings.contains("slaves")) {
            const auto configured = settings.contains("common_addresses")
                                        ? settings.at("common_addresses")
                                        : settings.at("slaves");
            for (const auto& address : configured) addresses.insert(address.get<int>());
        } else if (settings.contains("common_address")) {
            addresses.insert(settings.at("common_address").get<int>());
        }
        for (const auto& device : devices)
            for (const auto* points :
                 {&device.ycPoints_, &device.yxPoints_, &device.ykPoints_, &device.ytPoints_})
                for (const auto& point : *points)
                    addresses.insert(
                        std::stoi(point.protocol_code.substr(0, point.protocol_code.find(':'))));
        if (addresses.empty()) addresses.insert(1);
        settings.erase("common_address");
        settings.erase("slaves");
        settings["common_addresses"] = addresses;
    }
    channel.settings_ = settings.dump();
    return channel;
}

std::string connection_key(const ems::rtdb::ChannelConfig& channel, const std::string& scope) {
    using P = ems::model::ProtocolType;
    if (scope.empty()) return {};
    if (channel.protocol_ != P::ModbusTCP && channel.protocol_ != P::IEC104) return {};
    auto settings = settings_of(channel);
    if (settings.value("local_role", settings.value("role", std::string("client"))) != "client")
        return {};
    settings.erase("poll_interval_ms");
    if (channel.protocol_ == P::IEC104) settings.erase("common_addresses");
    auto endpoint = channel.connection_;
    std::transform(endpoint.begin(), endpoint.end(), endpoint.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return json::array({scope, static_cast<int>(channel.protocol_), endpoint, settings}).dump();
}

void merge_addresses(ems::rtdb::ChannelConfig& target, const ems::rtdb::ChannelConfig& source) {
    if (target.protocol_ != ems::model::ProtocolType::IEC104) return;
    auto settings = settings_of(target);
    auto addresses = settings.at("common_addresses").get<std::set<int>>();
    const auto source_settings = settings_of(source);
    for (const auto& address : source_settings.at("common_addresses"))
        addresses.insert(address.get<int>());
    settings["common_addresses"] = addresses;
    target.settings_ = settings.dump();
}
}  // namespace

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

    LOG_INFO(fmt::format("初始化采集引擎: 插件目录={}, RTDB配置={}, 默认周期={}ms",
                         config.pluginDir_, config.rtdbConfigPath_, config.defaultPollIntervalMs_)
                 .c_str());
    config_ = config;

    // 1. 设置插件目录
    plugin_manager_.set_plugin_dir(config.pluginDir_);

    // 2. 初始化数据分发器
    DataDispatcherConfig dispatchConfig;
    dispatchConfig.rtdbConfigPath_ = config.rtdbConfigPath_;
    dispatchConfig.rtdbScopeCode_ = config.rtdbScopeCode_;
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
        LOG_ERROR(fmt::format("数据分发器初始化失败, RTDB配置={}", config.rtdbConfigPath_).c_str());
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

int CollectEngine::start_all(CfgLoader& loader) {
    if (state_.load() == EngineState::Running) {
        LOG_WARN("采集引擎已在运行中");
        return 0;
    }

    if (!loader.is_initialized()) {
        LOG_ERROR("参数加载器未初始化");
        return enum_value(protocol::OperationResult::Failure);
    }

    // 1. 加载所有启用的通道配置
    auto channels = loader.channels();
    auto outputs = loader.forwarding_channels();
    if (!dispatcher_) return -1;
    if (channels.empty() && outputs.empty()) {
        LOG_INFO("数据库中没有启用的通道");
        return 0;
    }

    if (channels.size() + outputs.size() > static_cast<size_t>(config_.maxChannels_)) return -1;

    LOG_INFO(fmt::format("从数据库加载了{}个通道配置", channels.size()).c_str());

    struct Connection {
        ems::rtdb::ChannelConfig config;
        std::vector<ems::rtdb::ChannelConfig> members;
        std::vector<ConfiguredDevice> devices;
        int interval;
        std::string scope;
    };

    std::vector<Connection> connections;
    std::map<std::string, std::size_t> shared;
    for (const auto& logical : channels) {
        if (logical.status_ == 0) continue;
        auto devices = loader.devices(logical.id_);
        const int interval = std::max(
            enum_value(CollectDefaults::MinPollIntervalMs),
            settings_of(logical).value("poll_interval_ms", config_.defaultPollIntervalMs_));
        for (auto& device : devices) {
            device.channelId_ = logical.id_;
            device.pollIntervalMs_ = interval;
        }
        auto config = connection_config(logical, devices);
        const auto scope = loader.connection_scope(logical.id_);
        const auto key = connection_key(config, scope);
        const auto found = shared.find(key);
        if (!key.empty() && found != shared.end()) {
            auto& connection = connections[found->second];
            merge_addresses(connection.config, config);
            connection.interval = std::min(connection.interval, interval);
            connection.members.push_back(logical);
            connection.devices.insert(connection.devices.end(),
                                      std::make_move_iterator(devices.begin()),
                                      std::make_move_iterator(devices.end()));
        } else {
            if (!key.empty()) shared[key] = connections.size();
            connections.push_back(
                {std::move(config), {logical}, std::move(devices), interval, scope});
        }
    }

    // 每个物理连接只启动一个线程；原逻辑通道 ID 继续用于查询、控制和独立停止。
    int started = 0;
    for (auto& connection : connections) {
        auto& chConfig = connection.config;
        LOG_INFO(fmt::format("启动连接[{}] {}, 协议={}, 连接={}, 复用逻辑通道数={}", chConfig.id_,
                             chConfig.name_, ems::rtdb::protocolTypeToString(chConfig.protocol_),
                             chConfig.connection_, connection.members.size())
                     .c_str());
        auto channelThread = std::make_shared<ChannelThread>(chConfig, dispatcher_);
        channelThread->set_devices(std::move(connection.devices));
        channelThread->set_poll_interval(connection.interval);

        // 设置错误回调
        channelThread->set_error_callback(
            [this](int64_t chId, const std::string& err) { on_channel_error(chId, err); });

        if (!channelThread->start(plugin_manager_)) {
            for (const auto& member : connection.members)
                on_channel_error(member.id_, channelThread->last_error());
            continue;
        }

        // 注册到通道映射
        {
            std::lock_guard lock(channels_mutex_);
            for (const auto& member : connection.members) {
                channels_[member.id_] = channelThread;
                connection_scopes_[member.id_] = connection.scope;
                if (member.id_ != chConfig.id_)
                    LOG_INFO("通道[{}] {} 复用连接[{}] {}", member.id_, member.name_, chConfig.id_,
                             chConfig.connection_);
            }
        }

        started += static_cast<int>(connection.members.size());
    }

    for (auto& output : outputs) {
        const auto id = output.channel.id_;
        auto thread = std::make_shared<ForwardingChannel>(
            std::move(output), dispatcher_->rtdb_manager()->getRtDb(), plugin_manager_);
        if (!thread->start()) {
            on_channel_error(id, thread->last_error());
            continue;
        }
        std::lock_guard lock(channels_mutex_);
        forwarding_[id] = std::move(thread);
        ++started;
    }
    state_.store(EngineState::Running);
    LOG_INFO("采集引擎启动完成, 逻辑通道数={}, 独立连接数={}", started, connection_count());
    return started;
}

// ============================================================
// 启动/停止单个通道
// ============================================================

bool CollectEngine::start_channel(const ems::rtdb::ChannelConfig& channelConfig,
                                  const std::string& connection_scope) {
    if (channelConfig.status_ == 0) return false;
    std::lock_guard lock(channels_mutex_);

    if (channels_.size() + forwarding_.size() >= static_cast<size_t>(config_.maxChannels_)) {
        LOG_WARN(fmt::format("通道数量达到上限{}, 无法启动通道[{}]", config_.maxChannels_,
                             channelConfig.id_)
                     .c_str());
        return false;
    }

    // 检查是否已存在
    if (channels_.count(channelConfig.id_) || forwarding_.count(channelConfig.id_)) {
        LOG_WARN(fmt::format("通道[{}] 已存在, 跳过", channelConfig.id_).c_str());
        return false;
    }

    const auto config = connection_config(channelConfig, {});
    const auto key = connection_key(config, connection_scope);
    if (!key.empty()) {
        for (const auto& [id, worker] : channels_) {
            if (connection_key(worker->channel_config(), connection_scopes_.at(id)) != key)
                continue;
            if (config.protocol_ == ems::model::ProtocolType::IEC104) {
                const auto current = settings_of(worker->channel_config())
                                         .at("common_addresses")
                                         .get<std::set<int>>();
                const auto requested =
                    settings_of(config).at("common_addresses").get<std::set<int>>();
                if (!std::includes(current.begin(), current.end(), requested.begin(),
                                   requested.end())) {
                    LOG_ERROR("通道[{}] 新增公共地址，需要重载配置以重建共享连接",
                              channelConfig.id_);
                    return false;
                }
            }
            channels_[channelConfig.id_] = worker;
            connection_scopes_[channelConfig.id_] = connection_scope;
            LOG_INFO("通道[{}] {} 复用连接[{}] {}", channelConfig.id_, channelConfig.name_,
                     worker->channel_id(), channelConfig.connection_);
            return true;
        }
    }

    LOG_INFO(fmt::format("启动单个通道[{}] {}, 协议={}, 连接={}", channelConfig.id_,
                         channelConfig.name_,
                         ems::rtdb::protocolTypeToString(channelConfig.protocol_),
                         channelConfig.connection_)
                 .c_str());

    auto channelThread = std::make_shared<ChannelThread>(config, dispatcher_);
    channelThread->set_poll_interval(
        settings_of(config).value("poll_interval_ms", config_.defaultPollIntervalMs_));

    channels_[channelConfig.id_] = channelThread;
    connection_scopes_[channelConfig.id_] = connection_scope;

    if (!channelThread->start(plugin_manager_)) {
        LOG_ERROR(fmt::format("通道[{}] 处理器初始化失败: {}", channelConfig.id_,
                              channelThread->last_error())
                      .c_str());
        channels_.erase(channelConfig.id_);
        connection_scopes_.erase(channelConfig.id_);
        return false;
    }
    return true;
}

bool CollectEngine::stop_channel(int64_t channel_id) {
    LOG_INFO(fmt::format("停止单个通道[{}]", channel_id).c_str());
    std::shared_ptr<ChannelThread> channel;
    std::shared_ptr<ForwardingChannel> output;
    {
        std::lock_guard lock(channels_mutex_);
        const auto found = forwarding_.find(channel_id);
        if (found != forwarding_.end()) output = found->second;
    }
    if (output) {
        output->stop();
        std::lock_guard lock(channels_mutex_);
        forwarding_.erase(channel_id);
        return true;
    }
    {
        std::lock_guard lock(channels_mutex_);
        auto it = channels_.find(channel_id);
        if (it == channels_.end()) {
            LOG_WARN(fmt::format("通道[{}] 不存在", channel_id).c_str());
            return false;
        }
        channel = it->second;
        channel->remove_devices(channel_id);
        channels_.erase(it);
        connection_scopes_.erase(channel_id);
        const bool shared = std::any_of(channels_.begin(), channels_.end(),
                                        [&](const auto& entry) { return entry.second == channel; });
        if (shared) {
            LOG_INFO("逻辑通道[{}] 已停止，共享连接继续供其他通道使用", channel_id);
            return true;
        }
    }

    channel->stop();
    channel->join();

    {
        std::lock_guard lock(channels_mutex_);
        if (channels_.empty() && forwarding_.empty()) state_.store(EngineState::Stopped);
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
    std::vector<std::shared_ptr<ForwardingChannel>> outputs;
    {
        std::lock_guard lock(channels_mutex_);
        std::set<ChannelThread*> seen;
        for (auto& [_, ch] : channels_) {
            if (seen.insert(ch.get()).second) allChannels.push_back(ch);
        }
        for (auto& [_, output] : forwarding_) outputs.push_back(output);
    }

    // 北向会话先结束，之后才允许停止采集和关闭共享 RTDB 客户端。
    for (auto& output : outputs) output->stop();

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
        connection_scopes_.clear();
        forwarding_.clear();
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
    return channels_.size() + forwarding_.size();
}

size_t CollectEngine::connection_count() const {
    std::lock_guard lock(channels_mutex_);
    std::set<ChannelThread*> workers;
    for (const auto& [_, channel] : channels_) workers.insert(channel.get());
    return workers.size() + forwarding_.size();
}

size_t CollectEngine::running_count() const {
    std::lock_guard lock(channels_mutex_);
    return std::count_if(channels_.begin(), channels_.end(),
                         [](const auto& pair) { return pair.second->is_running(); }) +
           std::count_if(forwarding_.begin(), forwarding_.end(),
                         [](const auto& pair) { return pair.second->is_running(); });
}

std::vector<std::shared_ptr<ChannelThread>> CollectEngine::channels() const {
    std::lock_guard lock(channels_mutex_);
    std::vector<std::shared_ptr<ChannelThread>> result;
    result.reserve(channels_.size());
    std::set<ChannelThread*> seen;
    for (const auto& [_, ch] : channels_) {
        if (seen.insert(ch.get()).second) result.push_back(ch);
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

}  // namespace collect
}  // namespace net
}  // namespace ems
