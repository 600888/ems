/**
 * @file channel_thread.cpp
 * @brief 通道线程实现
 */
#include "channel_thread.h"

#include <spdlog/fmt/bundled/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <nlohmann/json.hpp>
#include <sstream>

#include "plugin_manager.h"

namespace ems {
namespace net {
namespace collect {

using json = nlohmann::json;

namespace {

uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// ============================================================
// 构造 / 析构
// ============================================================

ChannelThread::ChannelThread(const ChannelConfig& channelConfig,
                             std::shared_ptr<DataDispatcher> dispatcher)
    : channel_config_(channelConfig), dispatcher_(std::move(dispatcher)) {}

ChannelThread::~ChannelThread() {
    stop();
    join();
}

// ============================================================
// 生命周期管理
// ============================================================

bool ChannelThread::start(PluginManager& plugin_manager) {
    if (state_.load() != ChannelState::Created && state_.load() != ChannelState::Stopped)
        return false;
    join();
    set_state(ChannelState::Starting);
    try {
        handler_ = handler::create_handler(plugin_manager,
                                           rtdb::protocolTypeToString(channel_config_.protocol_));
        json config = {{"channel_id", channel_config_.id_},
                       {"name", channel_config_.name_},
                       {"protocol", rtdb::protocolTypeToString(channel_config_.protocol_)},
                       {"connection", channel_config_.connection_},
                       {"settings", channel_config_.settings_},
                       {"baud_rate", channel_config_.baudRate_},
                       {"data_bits", channel_config_.dataBits_},
                       {"stop_bits", channel_config_.stopBits_},
                       {"parity", channel_config_.parity_}};
        if (!handler_->initialize(config)) throw std::runtime_error(handler_->last_error());
        last_err_.clear();
        thread_ = std::make_shared<std::thread>([this]() {
            try {
                run();
            } catch (const std::exception& error) {
                last_err_ = error.what();
                set_state(ChannelState::Error);
            } catch (...) {
                last_err_ = "Unknown channel worker exception";
                set_state(ChannelState::Error);
            }
        });
        return true;
    } catch (const std::exception& error) {
        last_err_ = error.what();
        handler_.reset();
        set_state(ChannelState::Error);
        return false;
    }
}

void ChannelThread::stop() {
    auto s = state_.load();
    if (s == ChannelState::Stopped || s == ChannelState::Created) return;

    LOG_INFO(fmt::format("通道[{}] 收到停止请求", channel_config_.id_).c_str());
    CHANNEL_LOG_INFO(this, "收到停止请求");
    set_state(thread_ && thread_->joinable() ? ChannelState::Stopping : ChannelState::Stopped);
    cmd_cv_.notify_all();
}

void ChannelThread::join() {
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    if (state_.load() == ChannelState::Stopping) set_state(ChannelState::Stopped);
}

// ============================================================
// 配置
// ============================================================

void ChannelThread::set_poll_interval(int ms) {
    poll_interval_ms_.store(std::max(enum_value(CollectDefaults::MinPollIntervalMs), ms));
}

void ChannelThread::add_device(DeviceContext device) {
    std::lock_guard lock(device_mutex_);
    devices_.push_back(std::move(device));
}

void ChannelThread::set_devices(std::vector<DeviceContext> devices) {
    std::lock_guard lock(device_mutex_);
    devices_ = std::move(devices);
}

std::vector<DeviceContext> ChannelThread::devices() const {
    std::lock_guard lock(device_mutex_);
    return devices_;
}

// ============================================================
// 命令下发
// ============================================================

std::future<bool> ChannelThread::post_command(const std::string& pointCode, double value,
                                              int timeoutMs) {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    CommandTask task;
    task.taskId_ = now_ms();
    task.pointCode_ = pointCode;
    task.value_ = value;
    task.timeoutMs_ = timeoutMs;
    task.callback_ = [promise](bool success) { promise->set_value(success); };

    {
        std::lock_guard lock(cmd_mutex_);
        cmd_queue_.push_back(std::move(task));
    }
    cmd_cv_.notify_one();

    return future;
}

// ============================================================
// 线程主循环
// ============================================================

void ChannelThread::run() {
    // 所有退出分支都先结束最后一次处理器调用，再销毁实例并释放租约。
    auto release_handler = [this](void*) { handler_.reset(); };
    std::unique_ptr<void, decltype(release_handler)> lease(handler_.get(), release_handler);
    auto expected = ChannelState::Starting;
    if (!state_.compare_exchange_strong(expected, ChannelState::Running)) {
        set_state(ChannelState::Stopped);
        return;
    }
    CHANNEL_LOG_INFO(
        this, fmt::format("开始运行, 名称={}, 协议={}, 连接={}, 周期={}ms", channel_config_.name_,
                          ems::rtdb::protocolTypeToString(channel_config_.protocol_),
                          channel_config_.connection_, poll_interval_ms_.load())
                  .c_str());
    // 2. 连接设备
    if (!check_connection()) {
        // 连接失败，进入重试
        if (!reconnect()) {
            if (state_.load() == ChannelState::Stopping) {
                set_state(ChannelState::Stopped);
                return;
            }
            last_err_ = "Failed to connect channel " + std::to_string(channel_config_.id_);
            CHANNEL_LOG_ERROR(this, fmt::format("连接最终失败: {}", last_err_).c_str());
            set_state(ChannelState::Error);
            if (error_cb_) error_cb_(channel_config_.id_, last_err_);
            return;
        }
    }
    CHANNEL_LOG_INFO(this, "连接成功, 采集线程开始工作");

    // 3. 主采集循环
    while (state_.load() == ChannelState::Running) {
        auto cycleStart = now_ms();

        // 3.1 检查连接状态
        if (!check_connection()) {
            CHANNEL_LOG_WARN(this, "连接断开, 尝试重连...");
            reconnect();
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_.load()));
            continue;
        }

        // 3.2 处理命令队列（遥控/遥调优先）
        process_command_queue();

        // 3.3 逐设备采集
        std::vector<DeviceContext> currentDevices;
        {
            std::lock_guard lock(device_mutex_);
            currentDevices = devices_;
        }

        for (auto& device : currentDevices) {
            // 检查是否仍在运行（可能被外部停止）
            if (state_.load() != ChannelState::Running) break;

            collect_device(device);
        }

        // 3.4 计算等待时间
        auto elapsed = now_ms() - cycleStart;
        int interval = poll_interval_ms_.load();
        if (elapsed < static_cast<uint64_t>(interval)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(interval - static_cast<int>(elapsed)));
        }
        // 如果采集耗时超过周期，立即开始下一轮
    }

    // 4. 清理
    CHANNEL_LOG_INFO(this, "退出采集循环, 清理资源...");
    if (handler_ && !handler_->disconnect()) last_err_ = handler_->last_error();

    set_state(ChannelState::Stopped);
    CHANNEL_LOG_INFO(this, "已停止");
    LOG_INFO(fmt::format("通道[{}] 已停止", channel_config_.id_).c_str());
}

// ============================================================
// 连接管理
// ============================================================

bool ChannelThread::check_connection() { return handler_ && handler_->is_connected(); }

bool ChannelThread::reconnect() {
    if (!handler_) return false;

    CHANNEL_LOG_INFO(this, fmt::format("开始重连, 最大重试次数={}", max_retries_).c_str());
    int retries = 0;
    while (state_.load() == ChannelState::Running && retries < max_retries_) {
        int delay = backoff_delay();

        CHANNEL_LOG_DEBUG(this, fmt::format("重连第{}次, 等待{}ms...", retries + 1, delay).c_str());
        {
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            if (cmd_cv_.wait_for(lock, std::chrono::milliseconds(delay),
                                 [this] { return state_.load() != ChannelState::Running; }))
                return false;
        }

        if (handler_->connect()) {
            reconnect_count_.fetch_add(1);
            CHANNEL_LOG_INFO(
                this, fmt::format("重连成功, 已重连次数={}", reconnect_count_.load()).c_str());
            return true;
        }

        last_err_ = "Reconnect attempt " + std::to_string(retries + 1) +
                    " failed: " + handler_->last_error();
        CHANNEL_LOG_ERROR(this, fmt::format("重连失败: {}", last_err_).c_str());

        retries++;
    }

    CHANNEL_LOG_ERROR(this, fmt::format("重连达到最大次数({}), 放弃重连", max_retries_).c_str());
    if (error_cb_) {
        error_cb_(channel_config_.id_, "Max retries (" + std::to_string(max_retries_) +
                                           ") exceeded for channel " +
                                           std::to_string(channel_config_.id_));
    }

    return false;
}

// ============================================================
// 命令处理
// ============================================================

void ChannelThread::process_command_queue() {
    while (true) {
        CommandTask task;
        {
            std::lock_guard lock(cmd_mutex_);
            if (cmd_queue_.empty()) break;
            task = std::move(cmd_queue_.front());
            cmd_queue_.pop_front();
        }

        CHANNEL_LOG_INFO(this,
                         fmt::format("执行命令: taskId={}, pointCode={}, value={}, timeout={}ms",
                                     task.taskId_, task.pointCode_, task.value_, task.timeoutMs_)
                             .c_str());

        bool success = false;
        for (const auto& device : devices()) {
            for (const auto* routes : {&device.ykPoints_, &device.ytPoints_}) {
                for (const auto& route : *routes) {
                    if (route.point_id != task.pointCode_) continue;
                    double raw = task.value_;
                    if (route.write_mode == "fixed")
                        raw = route.fixed_raw_value;
                    else if (route.write_mode == "scaled") {
                        if (route.scale_factor == 0) continue;
                        raw = (raw - route.offset_value) / route.scale_factor;
                    }
                    success = handler_ && std::isfinite(raw) &&
                              handler_->write_value(route.protocol_code, raw);
                }
            }
        }

        CHANNEL_LOG_INFO(this, fmt::format("命令执行{}: taskId={}, pointCode={}",
                                           success ? "成功" : "失败", task.taskId_, task.pointCode_)
                                   .c_str());

        if (task.callback_) {
            task.callback_(success);
        }
    }
}

// ============================================================
// 测点采集
// ============================================================

void ChannelThread::collect_device(const DeviceContext& device) {
    collect_points(device.ycPoints_, "yc");
    collect_points(device.yxPoints_, "yx");
}

void ChannelThread::collect_points(const std::vector<device::PointRoute>& routes,
                                   const char* point_type) {
    if (routes.empty() || !handler_) return;
    std::vector<std::string> codes;
    for (const auto& route : routes) codes.push_back(route.protocol_code);
    std::vector<PointValue> values;
    if (handler_->read_batch(codes, values) < 0 || values.size() != routes.size()) {
        last_err_ = "read_batch failed or returned incomplete results: " + handler_->last_error();
        return;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& value = values[i];
        const auto& route = routes[i];
        value.code = route.point_id;
        if (std::string(point_type) == "yc")
            value.value = value.value * route.scale_factor + route.offset_value;
        else
            value.value = (value.value != 0) != route.invert ? 1.0 : 0.0;
        if (!value.details.is_object()) value.details = json::object();
        value.details["point_type"] = point_type;
    }
    if (dispatcher_ && !values.empty()) dispatcher_->batch_update(values);
}

// ============================================================
// 辅助方法
// ============================================================

void ChannelThread::set_state(ChannelState new_state) { state_.store(new_state); }

int ChannelThread::backoff_delay() const {
    int count = reconnect_count_.load();
    int delay =
        enum_value(CollectDefaults::ReconnectBaseDelayMs) *
        (1 << std::min(
             count,
             enum_value(CollectDefaults::ReconnectMaxExponent)));  // 指数退避，最多 2^6=64 倍
    return std::min(delay, enum_value(CollectDefaults::ReconnectMaxDelayMs));
}

}  // namespace collect
}  // namespace net
}  // namespace ems
