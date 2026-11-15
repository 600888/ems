/**
 * @file channel_thread.cpp
 * @brief 通道线程实现
 */
#include "channel_thread.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <map>
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
                             std::shared_ptr<DataDispatcher> dispatcher, HandlerFactory factory)
    : channel_config_(channelConfig),
      dispatcher_(std::move(dispatcher)),
      handler_factory_(std::move(factory)) {}

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
        handler_ = handler_factory_
                       ? handler_factory_(plugin_manager)
                       : handler::create_handler(
                             plugin_manager, rtdb::protocolTypeToString(channel_config_.protocol_));
        if (!handler_) throw std::runtime_error("Handler factory returned no instance");
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

void ChannelThread::remove_devices(int64_t channel_id) {
    std::lock_guard lock(device_mutex_);
    devices_.erase(
        std::remove_if(devices_.begin(), devices_.end(),
                       [&](const auto& device) { return device.channelId_ == channel_id; }),
        devices_.end());
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
    PointCommand command;
    command.point_id = pointCode;
    command.value = value;
    command.expires_at_ms = static_cast<std::int64_t>(now_ms()) + std::max(0, timeoutMs);
    if (!enqueue_command(
            command, [] { return true; },
            [promise](const auto& result) {
                promise->set_value(result.status == "Acked" || result.status == "Succeeded");
            }))
        promise->set_value(false);
    return future;
}

bool ChannelThread::enqueue_command(PointCommand command, BeforeCommand before_send,
                                    CommandCompletion completion) {
    if (!before_send || !completion) return false;
    const auto remaining =
        command.verify_only
            ? 5000
            : std::clamp<std::int64_t>(command.expires_at_ms - static_cast<std::int64_t>(now_ms()),
                                       0, 3600000);
    CommandTask task{std::move(command),
                     std::chrono::steady_clock::now() + std::chrono::milliseconds(remaining),
                     std::move(before_send), std::move(completion)};
    {
        std::lock_guard lock(cmd_mutex_);
        const auto state = state_.load();
        if ((state != ChannelState::Starting && state != ChannelState::Running) ||
            cmd_queue_.size() >= 128)
            return false;
        cmd_queue_.push_back(std::move(task));
    }
    cmd_cv_.notify_one();
    return true;
}

std::optional<device::PointRoute> ChannelThread::command_route(const std::string& point_id) const {
    for (const auto& device : devices())
        for (const auto* routes : {&device.ykPoints_, &device.ytPoints_})
            for (const auto& route : *routes)
                if (route.point_id == point_id) return route;
    return std::nullopt;
}

// ============================================================
// 线程主循环
// ============================================================

void ChannelThread::run() {
    // 所有退出分支都先结束最后一次处理器调用，再销毁实例并释放租约。
    auto release_handler = [this](void*) {
        cancel_commands();
        handler_.reset();
    };
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
    using Clock = std::chrono::steady_clock;
    std::map<std::pair<int64_t, std::string>, Clock::time_point> next_poll;
    while (state_.load() == ChannelState::Running) {
        auto cycleStart = now_ms();

        // 3.1 检查连接状态
        if (!check_connection()) {
            CHANNEL_LOG_WARN(this, "连接断开, 尝试重连...");
            reconnect();
            std::unique_lock lock(cmd_mutex_);
            cmd_cv_.wait_for(lock, std::chrono::milliseconds(poll_interval_ms_.load()),
                             [this] { return state_.load() != ChannelState::Running; });
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
            const auto identity = device.deviceKey_.empty()
                                      ? std::to_string(&device - currentDevices.data())
                                      : device.deviceKey_;
            const auto key = std::make_pair(device.channelId_, identity);
            const auto now = Clock::now();
            if (next_poll[key] > now) continue;
            const int interval =
                device.pollIntervalMs_ > 0 ? device.pollIntervalMs_ : poll_interval_ms_.load();
            next_poll[key] = now + std::chrono::milliseconds(interval);
            collect_device(device);
        }

        // 3.4 计算等待时间
        auto elapsed = now_ms() - cycleStart;
        int interval = poll_interval_ms_.load();
        if (elapsed < static_cast<uint64_t>(interval)) {
            std::unique_lock lock(cmd_mutex_);
            cmd_cv_.wait_for(
                lock, std::chrono::milliseconds(interval - static_cast<int>(elapsed)),
                [this] { return state_.load() != ChannelState::Running || !cmd_queue_.empty(); });
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
    CommandTask task;
    {
        std::lock_guard lock(cmd_mutex_);
        if (cmd_queue_.empty() || state_.load() != ChannelState::Running) return;
        task = std::move(cmd_queue_.front());
        cmd_queue_.pop_front();
    }
    PointCommandResult result;
    try {
        result = execute_command(task);
    } catch (const std::exception& error) {
        result = {"Unknown", error.what()};
    } catch (...) {
        result = {"Unknown", "unexpected command exception"};
    }
    try {
        task.completion(result);
    } catch (...) {
        LOG_ERROR("Command completion failed; persistent execution state must be reconciled");
    }
}

PointCommandResult ChannelThread::execute_command(const CommandTask& task) {
    const auto& command = task.command;
    if (!command.verify_only && (std::chrono::steady_clock::now() >= task.deadline ||
                                 static_cast<std::int64_t>(now_ms()) >= command.expires_at_ms))
        return {"Expired", "expired_before_send"};
    if (state_.load() != ChannelState::Running) return {"Rejected", "channel_stopping"};
    const auto route = command_route(command.point_id);
    if (!route) return {"Rejected", "control_point_not_found"};
    if (command.verify_only) {
        if (!task.before_send()) return {"Deferred", "verification_not_ready"};
        return verify_command(command, *route);
    }
    if (!std::isfinite(command.value)) return {"Rejected", "non_finite_value"};
    if (route->pulse_width_ms != 0) return {"Rejected", "pulse_control_not_supported"};
    if (route->write_mode != "fixed" && route->write_mode != "direct" &&
        route->write_mode != "scaled")
        return {"Rejected", "invalid_write_mode"};
    if (route->write_mode == "scaled" &&
        (command.value < route->range_min || command.value > route->range_max))
        return {"Rejected", "engineering_value_out_of_range"};
    double raw = command.value;
    if (route->write_mode == "fixed") raw = route->fixed_raw_value;
    if (route->write_mode == "scaled") {
        if (!std::isfinite(route->scale_factor) || route->scale_factor == 0)
            return {"Rejected", "invalid_scale"};
        raw = (raw - route->offset_value) / route->scale_factor;
    }
    if (!std::isfinite(raw)) return {"Rejected", "non_finite_raw_value"};
    const auto& wire = route->wire_type;
    if ((wire == "bool" && raw != 0 && raw != 1) ||
        ((wire == "u16" || wire == "i16" || wire == "u32" || wire == "i32") &&
         std::trunc(raw) != raw) ||
        (wire == "u16" && (raw < 0 || raw > 65535)) ||
        (wire == "i16" && (raw < -32768 || raw > 32767)) ||
        (wire == "u32" && (raw < 0 || raw > 4294967295.0)) ||
        (wire == "i32" && (raw < -2147483648.0 || raw > 2147483647.0)))
        return {"Rejected", "raw_value_out_of_range"};
    const auto group = route->write_group_key.empty() ? route->point_id : route->write_group_key;
    const auto last = last_command_.find(group);
    if (last != last_command_.end() && std::chrono::steady_clock::now() - last->second <
                                           std::chrono::milliseconds(route->min_interval_ms))
        return {"Rejected", "minimum_control_interval"};
    if (!handler_ || !handler_->is_connected()) return {"Deferred", "channel_disconnected"};
    if (!task.before_send()) return {"Deferred", "execution_registration_failed"};
    // 登记期间可能消耗期限；此时已登记 Executing，但可以证明未发送。
    if (state_.load() != ChannelState::Running) return {"Rejected", "channel_stopping"};
    if (std::chrono::steady_clock::now() >= task.deadline ||
        static_cast<std::int64_t>(now_ms()) >= command.expires_at_ms)
        return {"Expired", "expired_before_send"};
    last_command_[group] = std::chrono::steady_clock::now();
    const auto sent_at = static_cast<std::int64_t>(now_ms());
    const auto remaining_ms =
        std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                      task.deadline - std::chrono::steady_clock::now())
                                      .count());
    const auto write_deadline = std::min(command.expires_at_ms, sent_at + remaining_ms);
    if (!handler_->write_value_until(route->protocol_code, raw, write_deadline)) {
        const auto reason = handler_->last_error();
        if (channel_config_.protocol_ == ems::model::ProtocolType::IEC61850 &&
            (reason.rfind("IEC61850 not_sent:", 0) == 0 ||
             reason.rfind("IEC61850 rejected:", 0) == 0))
            return {"Rejected", reason};
        // 其他插件尚未给出可证明的失败阶段，保守记为 Unknown。
        return {"Unknown", reason, sent_at};
    }
    return {"Acked", route->feedback_json.empty() ? "protocol_acknowledged" : "feedback_pending",
            sent_at};
}

PointCommandResult ChannelThread::verify_command(const PointCommand& command,
                                                 const device::PointRoute& route) {
    if (command.sent_at_ms <= 0) return {"Unknown", "send_time_missing"};
    if (route.feedback_json.empty()) return {"Unknown", "feedback_not_configured"};
    const auto rules = json::parse(route.feedback_json);
    if (!rules.is_array() || rules.empty() || rules.size() > 32)
        return {"Unknown", "invalid_feedback_rules"};
    const auto current = devices();
    std::string owner;
    for (const auto& device : current)
        for (const auto* points : {&device.ykPoints_, &device.ytPoints_})
            for (const auto& point : *points)
                if (point.point_id == command.point_id) owner = device.deviceKey_;
    bool required = false;
    for (const auto& rule : rules) {
        if (!rule.value("required", true)) continue;
        required = true;
        const auto code = rule.at("point_code").get<std::string>();
        const device::PointRoute* feedback = nullptr;
        bool discrete = false;
        for (const auto& device : current) {
            if (device.deviceKey_ != owner) continue;
            for (const auto& point : device.ycPoints_)
                if (point.point_code == code) feedback = &point;
            for (const auto& point : device.yxPoints_)
                if (point.point_code == code) {
                    feedback = &point;
                    discrete = true;
                }
        }
        if (!feedback) return {"Unknown", "feedback_requires_same_connection"};
        std::vector<PointValue> values;
        if (!handler_ || handler_->read_batch({feedback->protocol_code}, values) != 1 ||
            values.size() != 1 || values[0].quality != enum_value(protocol::SampleQuality::Good))
            return {"Unknown", "feedback_unavailable"};
        const auto& sample = values[0];
        if (channel_config_.protocol_ == ems::model::ProtocolType::IEC104 &&
            (!sample.details.is_object() || !sample.details.contains("received_at_ms") ||
             !sample.details.at("received_at_ms").is_number_integer()))
            return {"Unknown", "feedback_receive_time_unavailable"};
        const auto received =
            sample.details.is_object()
                ? sample.details.value("received_at_ms",
                                       static_cast<std::int64_t>(sample.timestamp))
                : static_cast<std::int64_t>(sample.timestamp);
        if (received <= command.sent_at_ms) return {"Unknown", "feedback_not_newer_than_send"};
        const double observed =
            discrete ? static_cast<double>((sample.value != 0) != feedback->invert)
                     : sample.value * feedback->scale_factor + feedback->offset_value;
        const auto expected_value = rule.find("expected");
        const double expected = expected_value == rule.end() ? command.value
                                : expected_value->is_boolean()
                                    ? (expected_value->get<bool>() ? 1.0 : 0.0)
                                    : expected_value->get<double>();
        const auto criterion = rule.at("criterion").get<std::string>();
        const double tolerance = criterion == "equals" ? 0 : rule.value("tolerance", -1.0);
        if ((criterion != "equals" && criterion != "within_tolerance") ||
            !std::isfinite(expected) || !std::isfinite(tolerance) || tolerance < 0)
            return {"Unknown", "invalid_feedback_criterion"};
        if (!std::isfinite(observed) || std::abs(observed - expected) > tolerance)
            return {"Unknown", "feedback_mismatch"};
    }
    return required ? PointCommandResult{"Succeeded", "fresh_feedback_verified"}
                    : PointCommandResult{"Unknown", "required_feedback_missing"};
}

void ChannelThread::cancel_commands() {
    std::deque<CommandTask> pending;
    {
        std::lock_guard lock(cmd_mutex_);
        pending.swap(cmd_queue_);
    }
    for (auto& task : pending) {
        try {
            task.completion({"Rejected", "channel_stopped_before_send"});
        } catch (...) {
            LOG_ERROR("Command cancellation persistence failed");
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
    using Route = device::PointRoute;
    std::vector<std::vector<const Route*>> batches;
    const auto protocol = channel_config_.protocol_;
    if (protocol == ems::model::ProtocolType::ModbusTCP ||
        protocol == ems::model::ProtocolType::ModbusRTU) {
        struct Read {
            const Route* route;
            int slave, function, address, width;
        };

        std::vector<Read> reads;
        for (const auto& route : routes) {
            try {
                std::istringstream parts(route.protocol_code);
                std::string part;
                std::vector<int> fields;
                while (std::getline(parts, part, ':')) fields.push_back(std::stoi(part));
                if (fields.size() < 3) throw std::invalid_argument("address");
                const int decode = fields.size() > 3 ? fields[3] : 0;
                const int width = decode == 5 ? 4 : (decode >= 2 && decode <= 4 ? 2 : 1);
                reads.push_back({&route, fields[0], fields[1], fields[2], width});
            } catch (...) {
                batches.push_back({&route});
            }
        }
        std::sort(reads.begin(), reads.end(), [](const auto& a, const auto& b) {
            return std::tie(a.slave, a.function, a.address) <
                   std::tie(b.slave, b.function, b.address);
        });
        for (std::size_t first = 0; first < reads.size();) {
            const auto& begin = reads[first];
            int end = begin.address + begin.width;
            std::size_t next = first + 1;
            std::vector<const Route*> batch{begin.route};
            const int maximum = begin.function <= 2 ? 2000 : 125;
            while (next < reads.size()) {
                const auto& read = reads[next];
                const int extended = std::max(end, read.address + read.width);
                if (read.slave != begin.slave || read.function != begin.function ||
                    read.address > end || extended - begin.address > maximum)
                    break;
                batch.push_back(read.route);
                end = extended;
                ++next;
            }
            batches.push_back(std::move(batch));
            first = next;
        }
    } else {
        // IEC104 读取的是接收缓存；其他协议保留原批量能力。
        const std::size_t maximum =
            protocol == ems::model::ProtocolType::IEC104 ? 64 : routes.size();
        for (std::size_t i = 0; i < routes.size(); i += maximum) {
            std::vector<const Route*> batch;
            for (std::size_t j = i; j < std::min(i + maximum, routes.size()); ++j)
                batch.push_back(&routes[j]);
            batches.push_back(std::move(batch));
        }
    }
    for (const auto& batch : batches) {
        if (state_.load() != ChannelState::Running || !handler_) return;
        process_command_queue();
        if (state_.load() != ChannelState::Running || !handler_) return;
        std::vector<std::string> codes;
        for (const auto* route : batch) codes.push_back(route->protocol_code);
        std::vector<PointValue> values;
        if (handler_->read_batch(codes, values) < 0 || values.size() != batch.size()) {
            last_err_ = "read_batch failed: " + handler_->last_error();
            continue;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto& value = values[i];
            const auto& route = *batch[i];
            value.code = route.point_id;
            if (std::string(point_type) == "yc")
                value.value = value.value * route.scale_factor + route.offset_value;
            else
                value.value = (value.value != 0) != route.invert ? 1.0 : 0.0;
            if (!value.details.is_object()) value.details = json::object();
            value.details["point_type"] = point_type;
        }
        if (dispatcher_) dispatcher_->batch_update(values);
    }
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
