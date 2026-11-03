#include "forwarding_channel.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace ems::net::collect {
namespace {
using Json = nlohmann::json;

std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool fits(const std::string& type, double value) {
    if (!std::isfinite(value)) return false;
    if (type == "boolean") return value == 0 || value == 1;
    if (type == "integer")
        return std::trunc(value) == value && value >= INT32_MIN && value <= INT32_MAX;
    if (type == "unsigned") return std::trunc(value) == value && value >= 0 && value <= UINT32_MAX;
    if (type == "float32") return std::abs(value) <= std::numeric_limits<float>::max();
    return type == "float64";
}
}  // namespace

class ForwardingChannel::Impl {
   public:
    device::ForwardingChannelConfig config;
    std::shared_ptr<rtdb::RtDb> realtime;
    PluginManager& manager;
    PluginManager::PluginFunctions api;
    void* instance{nullptr};
    std::thread worker;
    std::atomic<bool> running{false};
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping{false};
    std::string error;
    int interval{1000}, timeout{3000};
    std::map<std::string, Json> applied;

    Impl(device::ForwardingChannelConfig settings, std::shared_ptr<rtdb::RtDb> client,
         PluginManager& plugins)
        : config(std::move(settings)), realtime(std::move(client)), manager(plugins) {}

    void failure(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        error = message;
    }

    Json invalid(const device::ForwardingPoint& point) const {
        const auto previous = applied.find(point.code);
        const auto timestamp = previous == applied.end()
                                   ? now_ms()
                                   : previous->second.at("timestamp").get<std::uint64_t>();
        return Json{
            {"code", point.code}, {"value", nullptr}, {"iec_quality", 1}, {"timestamp", timestamp}};
    }

    Json sample(const device::ForwardingPoint& point, const core::CallContext& ctx) {
        auto output = invalid(point);
        if (!point.source_enabled || core::has_expired(ctx)) return output;
        auto result = realtime->readPointJson(point.point_type, point.source_point_id, ctx);
        if (!core::is_ok(result)) return output;
        const auto input = Json::parse(core::value_of(result));
        if (input.at("point_type") != (point.point_type == "yc" ? "YC" : "YX") ||
            !input.at("quality").is_number_integer() || !input.at("timestamp").is_number_integer())
            return output;
        const auto time = input.at("timestamp").get<std::int64_t>();
        const auto now = now_ms();
        if (time <= 0 || time > 4294967295999LL || static_cast<std::uint64_t>(time) > now + 5000)
            return output;
        output["timestamp"] = time;
        const bool stale = now > static_cast<std::uint64_t>(time) &&
                           now - time > static_cast<std::uint64_t>(point.stale_after_ms);
        if (stale) output["iec_quality"] = 1 | 128;  // invalid | oldData
        const auto& value = input.at("value");
        if (!value.is_number() && !value.is_boolean()) return output;
        const double numeric =
            value.is_boolean() ? (value.get<bool>() ? 1.0 : 0.0) : value.get<double>();
        if (!fits(point.mms_type, numeric)) return output;
        output["value"] = numeric;
        if (!stale && input.at("quality").get<std::int64_t>() == 0) output["iec_quality"] = 0;
        return output;
    }

    bool sync(bool invalidate = false) {
        const auto ctx = core::make_call_context(std::chrono::milliseconds(timeout));
        Json changes = Json::array();
        bool missing = false;
        for (const auto& point : config.points) {
            auto output = invalid(point);
            try {
                if (!invalidate) output = sample(point, ctx);
            } catch (...) {
                // 单点记录损坏不阻止其他点变为有效；旧映像的质量仍须失效。
            }
            missing = missing || output["iec_quality"] != 0;
            const auto previous = applied.find(point.code);
            if (previous == applied.end() || previous->second != output)
                changes.push_back(std::move(output));
        }
        if (!changes.empty() && api.update_image_fn(instance, changes.dump().c_str()) != 0) {
            failure("MMS image update failed");
            return false;
        }
        for (const auto& item : changes) applied[item.at("code").get<std::string>()] = item;
        failure(missing ? "RTDB source unavailable, invalid or stale" : "");
        return true;
    }
};

ForwardingChannel::ForwardingChannel(device::ForwardingChannelConfig config,
                                     std::shared_ptr<rtdb::RtDb> realtime, PluginManager& manager)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(realtime), manager)) {}

ForwardingChannel::~ForwardingChannel() { stop(); }

bool ForwardingChannel::start() {
    if (impl_->running) return true;
    stop();
    if (impl_->instance) {
        impl_->failure("previous MMS server could not stop");
        return false;
    }
    try {
        if (!impl_->realtime || impl_->config.points.empty())
            throw std::runtime_error("forwarding requires RTDB and point mappings");
        auto settings = Json::parse(impl_->config.channel.settings_);
        impl_->interval = settings.value("poll_interval_ms", 1000);
        impl_->timeout = settings.value("timeout_ms", 3000);
        if (impl_->interval < 1 || impl_->timeout < 1)
            throw std::runtime_error("invalid forwarding timing");
        impl_->instance = impl_->manager.load("IEC61850");
        if (!impl_->instance) throw std::runtime_error("cannot load IEC61850 forwarding plugin");
        impl_->api = impl_->manager.get_instance_functions(impl_->instance);
        if (!impl_->api.update_image_fn)
            throw std::runtime_error("plugin does not support image updates");
        const Json config{{"connection", impl_->config.channel.connection_},
                          {"settings", settings}};
        if (impl_->api.init_fn(impl_->instance, config.dump().c_str()) != 0)
            throw std::runtime_error("invalid MMS server model");
        impl_->applied.clear();
        if (!impl_->sync(true) || !impl_->sync() || impl_->api.connect_fn(impl_->instance) != 0)
            throw std::runtime_error("cannot start MMS forwarding server");
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->stopping = false;
        }
        impl_->running = true;
        impl_->worker = std::thread([state = impl_.get()] {
            try {
                while (true) {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    if (state->wake.wait_for(lock, std::chrono::milliseconds(state->interval),
                                             [&] { return state->stopping; }))
                        break;
                    lock.unlock();
                    if (!state->sync() || state->api.is_connected_fn(state->instance) != 1) {
                        state->sync(true);
                        state->failure("MMS forwarding worker stopped");
                        break;
                    }
                }
            } catch (...) {
                try {
                    state->sync(true);
                } catch (...) {
                }
                state->failure("MMS forwarding worker failed");
            }
            state->api.disconnect_fn(state->instance);
            state->running = false;
        });
        return true;
    } catch (const std::exception& error) {
        stop();
        impl_->failure(error.what());
        return false;
    }
}

void ForwardingChannel::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->running = false;
    if (impl_->instance && impl_->manager.release(impl_->instance)) impl_->instance = nullptr;
}

bool ForwardingChannel::is_running() const { return impl_->running; }

std::string ForwardingChannel::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}
}  // namespace ems::net::collect
