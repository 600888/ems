#include <common/codec/event_codec.h>
#include <common/core/identity.h>
#include <common/messaging/zmq_change_channel.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "telemetry_feed.h"

namespace ems::storage {
namespace {

std::string instance_prefix() {
    std::random_device random;
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(tick) + "-" + std::to_string(random());
}

class ZmqTelemetryFeed final : public IHistoryFeed {
   public:
    explicit ZmqTelemetryFeed(ZmqTelemetryOptions options)
        : options_(std::move(options)), prefix_(instance_prefix()) {}

    bool start() {
        messaging::ZmqChangeConfig config;
        config.endpoint = options_.endpoint;
        config.max_queue_items = options_.max_queue_items;
        config.max_message_bytes = options_.max_message_bytes;
        config.high_water_mark = options_.high_water_mark;
        return subscriber_.start(config, {options_.topic},
                                 [this](const std::string& topic, const std::string& raw) {
                                     if (topic == options_.topic) on_message(raw);
                                 });
    }

    core::Result<std::vector<messaging::Delivery>> read(std::uint64_t max_items,
                                                        std::uint64_t max_bytes,
                                                        const core::CallContext& ctx) override {
        if (max_items == 0 || max_bytes == 0) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "telemetry read bounds");
        }
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty() && !closed_ && !core::has_expired(ctx)) {
            const auto wait_ms = std::max<std::int64_t>(
                1, std::min<std::int64_t>(1000, core::remaining_budget_ms(ctx)));
            available_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                [this] { return closed_ || !queue_.empty(); });
        }
        std::vector<RawTelemetry> popped;
        std::uint64_t bytes = 0;
        while (!queue_.empty() && popped.size() < max_items) {
            const auto next_size = queue_.front().raw.size();
            if (popped.empty() && next_size > max_bytes) {
                queue_.pop_front();
                ++stats_.dropped_local;
                continue;
            }
            if (!popped.empty() && next_size > max_bytes - bytes) break;
            bytes += next_size;
            popped.push_back(std::move(queue_.front()));
            queue_.pop_front();
            if (bytes >= max_bytes) break;
        }
        lock.unlock();
        std::vector<messaging::Delivery> result;
        result.reserve(popped.size());
        for (auto& item : popped) {
            messaging::EventEnvelope event;
            bool valid = false;
            try {
                auto decoded = codec::decode_event(item.raw);
                if (core::is_ok(decoded)) {
                    event = core::value_of(decoded);
                    valid = true;
                }
            } catch (...) {
            }
            if (!valid) {
                event.schema_version = "invalid";
                event.event_id = core::EventId("invalid-" + item.token);
                event.payload_json = item.raw;
            }
            result.push_back({std::move(event), {item.token}, item.token, 1, std::move(item.raw)});
        }
        return result;
    }

    FeedStatistics stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    void close() noexcept override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
            stats_.dropped_local += queue_.size();
            queue_.clear();
        }
        available_.notify_all();
        subscriber_.stop();
    }

   private:
    struct RawTelemetry {
        std::string token;
        std::string raw;
    };

    void on_message(const std::string& raw) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.received;
        if (closed_ || queue_.size() >= options_.max_queue_items) {
            ++stats_.dropped_local;
            return;
        }
        const auto token = prefix_ + "-" + std::to_string(++next_id_);
        queue_.push_back({token, raw});
        available_.notify_one();
    }

    ZmqTelemetryOptions options_;
    std::string prefix_;
    messaging::ZmqChangeSubscriber subscriber_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<RawTelemetry> queue_;
    FeedStatistics stats_;
    std::uint64_t next_id_{0};
    bool closed_{false};
};

}  // namespace

core::Result<std::unique_ptr<IHistoryFeed>> connect_zmq_telemetry(
    const ZmqTelemetryOptions& options) {
    if (options.endpoint.empty() || options.topic.empty() || options.max_queue_items == 0 ||
        options.max_message_bytes == 0 || options.high_water_mark <= 0) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "ZeroMQ telemetry settings");
    }
    auto feed = std::make_unique<ZmqTelemetryFeed>(options);
    if (!feed->start()) {
        return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::NotApplicable,
                                "ZeroMQ telemetry subscribe");
    }
    return std::unique_ptr<IHistoryFeed>(std::move(feed));
}

}  // namespace ems::storage
