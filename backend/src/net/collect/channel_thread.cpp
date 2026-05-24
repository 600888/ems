/**
 * @file channel_thread.cpp
 * @brief 通道线程实现
 */
#include "channel_thread.h"
#include "plugin_manager.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <future>
#include <nlohmann/json.hpp>

namespace ems {
namespace net {
namespace collect {

using json = nlohmann::json;

namespace {
    constexpr int BASE_DELAY_MS = 1000;     // 重连基础延迟 1s
    constexpr int MAX_DELAY_MS  = 60000;    // 最大重连延迟 60s
    constexpr int RECONNECT_BACKOFF_FACTOR = 2;

    uint64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    /**
     * @brief 从插件 last_error C 接口获取错误字符串
     */
    std::string get_plugin_error(void* instance,
                                  ems_plugin_last_error_fn last_error_fn) {
        if (!instance || !last_error_fn) return {};
        const char* err = last_error_fn(instance);
        return err ? std::string(err) : std::string();
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

ChannelThread::ChannelThread(const ChannelConfig& channelConfig,
                              std::shared_ptr<DataDispatcher> dispatcher)
    : channel_config_(channelConfig)
    , dispatcher_(std::move(dispatcher))
{
}

ChannelThread::~ChannelThread() {
    stop();
    join();
}

// ============================================================
// 生命周期管理
// ============================================================

void ChannelThread::start(PluginManager& plugin_manager,
                           void* plugin_instance,
                           const PluginManager::PluginFunctions& plugin_fns) {
    if (state_.load() != ChannelState::Created &&
        state_.load() != ChannelState::Stopped) {
        return;
    }

    plugin_instance_ = plugin_instance;
    plugin_fns_      = plugin_fns;
    set_state(ChannelState::Starting);

    thread_ = std::make_shared<std::thread>([this, &plugin_manager]() {
        run(plugin_manager);
    });
}

void ChannelThread::stop() {
    auto s = state_.load();
    if (s == ChannelState::Stopped || s == ChannelState::Created) return;

    set_state(ChannelState::Stopping);
    cmd_cv_.notify_all();
}

void ChannelThread::join() {
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

// ============================================================
// 配置
// ============================================================

void ChannelThread::set_poll_interval(int ms) {
    poll_interval_ms_.store(std::max(100, ms));
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

std::future<bool> ChannelThread::post_command(const std::string& pointCode,
                                               double value, int timeoutMs) {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();

    CommandTask task;
    task.taskId_    = now_ms();
    task.pointCode_ = pointCode;
    task.value_     = value;
    task.timeoutMs_ = timeoutMs;
    task.callback_  = [promise](bool success) {
        promise->set_value(success);
    };

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

void ChannelThread::run(PluginManager& plugin_manager) {
    set_state(ChannelState::Running);

    // 1. 初始化插件（传入 JSON 配置）
    json initConfig;
    initConfig["channel_id"]   = channel_config_.id_;
    initConfig["name"]         = channel_config_.name_;
    initConfig["protocol"]     = ems::rtdb::protocolTypeToString(channel_config_.protocol_);
    initConfig["connection"]   = channel_config_.connection_;
    initConfig["settings"]     = channel_config_.settings_;
    initConfig["baud_rate"]    = channel_config_.baudRate_;
    initConfig["data_bits"]    = channel_config_.dataBits_;
    initConfig["stop_bits"]    = channel_config_.stopBits_;
    initConfig["parity"]       = channel_config_.parity_;

    std::string configStr = initConfig.dump();
    if (plugin_fns_.init_fn) {
        int ret = plugin_fns_.init_fn(plugin_instance_, configStr.c_str());
        if (ret != 0) {
            last_err_ = "Plugin init failed for channel " +
                        std::to_string(channel_config_.id_) + ": " +
                        get_plugin_error(plugin_instance_, plugin_fns_.last_error_fn);
            set_state(ChannelState::Error);
            if (error_cb_) error_cb_(channel_config_.id_, last_err_);
            plugin_manager.unload(channel_config_.name_);
            return;
        }
    }

    // 2. 连接设备
    if (!check_connection()) {
        // 连接失败，进入重试
        if (!reconnect()) {
            last_err_ = "Failed to connect channel " +
                        std::to_string(channel_config_.id_);
            set_state(ChannelState::Error);
            if (error_cb_) error_cb_(channel_config_.id_, last_err_);
            plugin_manager.unload(channel_config_.name_);
            return;
        }
    }

    // 3. 主采集循环
    while (state_.load() == ChannelState::Running) {
        auto cycleStart = now_ms();

        // 3.1 检查连接状态
        if (!check_connection()) {
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

            // 采集遥测
            if (!device.ycPointCodes_.empty()) {
                collect_points(device.ycPointCodes_);
            }

            // 采集遥信
            if (!device.yxPointCodes_.empty()) {
                collect_points(device.yxPointCodes_);
            }
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
    if (plugin_fns_.disconnect_fn && plugin_instance_) {
        plugin_fns_.disconnect_fn(plugin_instance_);
    }

    set_state(ChannelState::Stopped);
}

// ============================================================
// 连接管理
// ============================================================

bool ChannelThread::check_connection() {
    if (!plugin_instance_ || !plugin_fns_.is_connected_fn) return false;
    int ret = plugin_fns_.is_connected_fn(plugin_instance_);
    return ret == 1;
}

bool ChannelThread::reconnect() {
    if (!plugin_instance_ || !plugin_fns_.connect_fn) return false;

    int retries = 0;
    while (state_.load() == ChannelState::Running && retries < max_retries_) {
        int delay = backoff_delay();

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));

        int ret = plugin_fns_.connect_fn(plugin_instance_);
        if (ret == 0) {
            reconnect_count_.fetch_add(1);
            return true;
        }

        last_err_ = "Reconnect attempt " + std::to_string(retries + 1) +
                    " failed: " +
                    get_plugin_error(plugin_instance_, plugin_fns_.last_error_fn);

        retries++;
    }

    if (error_cb_) {
        error_cb_(channel_config_.id_,
                  "Max retries (" + std::to_string(max_retries_) +
                  ") exceeded for channel " + std::to_string(channel_config_.id_));
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

        // 通过插件写入
        bool success = false;
        if (plugin_fns_.write_fn && plugin_instance_) {
            int ret = plugin_fns_.write_fn(plugin_instance_,
                                           task.pointCode_.c_str(),
                                           task.value_);
            success = (ret == 0);
        }

        if (task.callback_) {
            task.callback_(success);
        }
    }
}

// ============================================================
// 测点采集
// ============================================================

void ChannelThread::collect_device(const DeviceContext& device) {
    if (!device.ycPointCodes_.empty()) {
        collect_points(device.ycPointCodes_);
    }

    if (!device.yxPointCodes_.empty()) {
        collect_points(device.yxPointCodes_);
    }
}

void ChannelThread::collect_points(const std::vector<std::string>& pointCodes) {
    if (pointCodes.empty() || !plugin_instance_ || !plugin_fns_.read_batch_fn) {
        return;
    }

    // 准备 C 风格的测点编码数组
    std::vector<const char*> cCodes;
    cCodes.reserve(pointCodes.size());
    for (const auto& code : pointCodes) {
        cCodes.push_back(code.c_str());
    }

    // 分配结果缓冲区
    char resultBuf[EMS_PLUGIN_RESULT_BUF_SIZE] = {0};

    int readCount = plugin_fns_.read_batch_fn(
        plugin_instance_,
        cCodes.data(),
        static_cast<int>(cCodes.size()),
        resultBuf,
        sizeof(resultBuf)
    );

    if (readCount < 0) {
        // 读取失败
        last_err_ = "read_batch failed: " +
                    get_plugin_error(plugin_instance_, plugin_fns_.last_error_fn);
        return;
    }

    if (readCount == 0) return;

    // 解析 JSON 结果并分发
    try {
        auto results = json::parse(resultBuf);
        std::vector<PointValue> pointValues;
        pointValues.reserve(results.size());

        for (const auto& item : results) {
            PointValue pv;
            pv.code      = item.value("code", std::string());
            pv.value     = item.value("value", 0.0);
            pv.quality   = item.value("quality", 1);
            pv.timestamp = item.value("timestamp", now_ms());
            pointValues.push_back(std::move(pv));
        }

        if (dispatcher_ && !pointValues.empty()) {
            dispatcher_->batch_update(pointValues);
        }
    } catch (const json::parse_error& e) {
        last_err_ = "Failed to parse read_batch result: " + std::string(e.what());
    }
}

// ============================================================
// 辅助方法
// ============================================================

void ChannelThread::set_state(ChannelState new_state) {
    state_.store(new_state);
}

int ChannelThread::backoff_delay() const {
    int count = reconnect_count_.load();
    int delay = BASE_DELAY_MS * (1 << std::min(count, 6)); // 指数退避，最多 2^6=64 倍
    return std::min(delay, MAX_DELAY_MS);
}

} // namespace collect
} // namespace net
} // namespace ems
