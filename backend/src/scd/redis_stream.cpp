#include <common/codec/event_codec.h>
#include <common/core/identity.h>
#include <hiredis.h>
#include <storage/native_adapters.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ems::storage {
namespace {

core::Error redis_error(const char* operation, core::WriteEffect effect) {
    return core::make_error(core::ErrorCode::Unavailable, effect,
                            std::string("Redis ") + operation + " failed");
}

struct ContextCloser {
    void operator()(redisContext* context) const {
        if (context) redisFree(context);
    }
};

struct ReplyCloser {
    void operator()(redisReply* reply) const {
        if (reply) freeReplyObject(reply);
    }
};

using ContextPtr = std::unique_ptr<redisContext, ContextCloser>;
using ReplyPtr = std::unique_ptr<redisReply, ReplyCloser>;

std::string reply_text(const redisReply* reply) {
    if (!reply || !reply->str || reply->len < 0) return {};
    return {reply->str, static_cast<std::size_t>(reply->len)};
}

class RedisStreamBus final : public messaging::IEventBus {
   public:
    explicit RedisStreamBus(RedisStreamOptions options) : options_(std::move(options)) {}

    core::Status connect() {
        std::lock_guard<std::mutex> lock(mutex_);
        return ensure_connected();
    }

    core::Result<messaging::PublishReceipt> publish(const std::string&, const std::string&,
                                                    const messaging::EventEnvelope&,
                                                    const core::CallContext&) override {
        return core::make_error(core::ErrorCode::Unsupported, core::WriteEffect::NotApplied,
                                "storage Redis adapter consumes telemetry only");
    }

    core::Result<std::vector<messaging::Delivery>> read(const messaging::Subscription& subscription,
                                                        std::uint64_t max_items,
                                                        std::uint64_t max_bytes,
                                                        const core::CallContext& ctx) override {
        if (subscription.group != options_.group || subscription.topic != "telemetry" ||
            max_items == 0 || max_items > 1024 || max_bytes == 0) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "Redis subscription");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto ready = ensure_connected();
        if (!core::is_ok(ready)) return core::error_of(ready);
        auto pending = read_once(subscription, max_items, "0", 0);
        if (!core::is_ok(pending)) return core::error_of(pending);
        auto deliveries = std::move(std::get<std::vector<messaging::Delivery>>(pending));
        if (deliveries.size() < max_items && !core::has_expired(ctx)) {
            const auto block_ms =
                deliveries.empty()
                    ? std::max<std::int64_t>(
                          1, std::min<std::int64_t>(1000, core::remaining_budget_ms(ctx)))
                    : 0;
            auto fresh = read_once(subscription, max_items - deliveries.size(), ">", block_ms);
            if (!core::is_ok(fresh)) return core::error_of(fresh);
            auto new_deliveries = std::move(std::get<std::vector<messaging::Delivery>>(fresh));
            deliveries.insert(deliveries.end(), std::make_move_iterator(new_deliveries.begin()),
                              std::make_move_iterator(new_deliveries.end()));
        }
        std::uint64_t total_bytes = 0;
        std::vector<messaging::Delivery> bounded;
        bounded.reserve(deliveries.size());
        for (auto& delivery : deliveries) {
            const auto size = delivery.event.payload_json.size();
            if (size > max_bytes - std::min(max_bytes, total_bytes) && !bounded.empty()) break;
            total_bytes += size;
            bounded.push_back(std::move(delivery));
            if (total_bytes >= max_bytes) break;
        }
        return bounded;
    }

    core::Status ack(const messaging::Subscription& subscription, const std::string& delivery_token,
                     const core::CallContext& ctx) override {
        if (subscription.group != options_.group || delivery_token.empty() ||
            core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                    "Redis ACK arguments");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto ready = ensure_connected();
        if (!core::is_ok(ready)) return core::error_of(ready);
        auto reply = command({"XACK", options_.stream_key, options_.group, delivery_token});
        if (!core::is_ok(reply)) return core::error_of(reply);
        return core::ok_status();
    }

    core::Result<std::uint64_t> reclaim(const messaging::Subscription& subscription,
                                        std::chrono::milliseconds min_idle, std::uint64_t max_items,
                                        const core::CallContext& ctx) override {
        if (subscription.group != options_.group || min_idle.count() < 0 || max_items == 0 ||
            max_items > 1024 || core::has_expired(ctx)) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "Redis reclaim arguments");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto ready = ensure_connected();
        if (!core::is_ok(ready)) return core::error_of(ready);
        auto reply = command({"XAUTOCLAIM", options_.stream_key, options_.group,
                              subscription.consumer, std::to_string(min_idle.count()),
                              reclaim_cursor_, "COUNT", std::to_string(max_items)});
        if (!core::is_ok(reply)) return core::error_of(reply);
        const auto* result = std::get<ReplyPtr>(reply).get();
        if (result->type != REDIS_REPLY_ARRAY || result->elements < 2 ||
            result->element[1]->type != REDIS_REPLY_ARRAY) {
            return redis_error("reclaim response", core::WriteEffect::NotApplicable);
        }
        const auto next = reply_text(result->element[0]);
        if (next.empty()) return redis_error("reclaim cursor", core::WriteEffect::NotApplicable);
        reclaim_cursor_ = next;
        return static_cast<std::uint64_t>(result->element[1]->elements);
    }

    core::Status close(const core::CallContext&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_.reset();
        return core::ok_status();
    }

   private:
    core::Status ensure_connected() {
        if (connection_ && !connection_->err) return core::ok_status();
        connection_.reset();
        timeval timeout{5, 0};
        connection_.reset(redisConnectWithTimeout(options_.host.c_str(), options_.port, timeout));
        if (!connection_ || connection_->err) {
            connection_.reset();
            return redis_error("connect", core::WriteEffect::NotApplicable);
        }
        redisSetTimeout(connection_.get(), timeout);
        if (!options_.password.empty()) {
            const auto arguments =
                options_.user.empty()
                    ? std::vector<std::string>{"AUTH", options_.password}
                    : std::vector<std::string>{"AUTH", options_.user, options_.password};
            auto authenticated = command(arguments);
            if (!core::is_ok(authenticated)) {
                connection_.reset();
                return core::error_of(authenticated);
            }
        }
        auto group =
            command({"XGROUP", "CREATE", options_.stream_key, options_.group, "0", "MKSTREAM"});
        if (!core::is_ok(group) && core::error_of(group).code != core::ErrorCode::Conflict) {
            connection_.reset();
            return core::error_of(group);
        }
        return core::ok_status();
    }

    core::Result<ReplyPtr> command(const std::vector<std::string>& arguments) {
        std::vector<const char*> values;
        std::vector<std::size_t> lengths;
        values.reserve(arguments.size());
        lengths.reserve(arguments.size());
        for (const auto& argument : arguments) {
            values.push_back(argument.data());
            lengths.push_back(argument.size());
        }
        ReplyPtr reply(static_cast<redisReply*>(redisCommandArgv(
            connection_.get(), static_cast<int>(arguments.size()), values.data(), lengths.data())));
        if (!reply) return redis_error("command", core::WriteEffect::Unknown);
        if (reply->type == REDIS_REPLY_ERROR) {
            const auto message = reply_text(reply.get());
            if (message.rfind("BUSYGROUP", 0) == 0) {
                return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                        "Redis group exists");
            }
            return redis_error("command", core::WriteEffect::NotApplied);
        }
        return reply;
    }

    core::Result<std::vector<messaging::Delivery>> read_once(
        const messaging::Subscription& subscription, std::uint64_t count, const std::string& cursor,
        std::int64_t block_ms) {
        std::vector<std::string> arguments{"XREADGROUP",          "GROUP", options_.group,
                                           subscription.consumer, "COUNT", std::to_string(count)};
        if (block_ms > 0) {
            arguments.push_back("BLOCK");
            arguments.push_back(std::to_string(block_ms));
        }
        arguments.insert(arguments.end(), {"STREAMS", options_.stream_key, cursor});
        auto reply = command(arguments);
        if (!core::is_ok(reply)) return core::error_of(reply);
        const auto* top = std::get<ReplyPtr>(reply).get();
        if (top->type == REDIS_REPLY_NIL) return std::vector<messaging::Delivery>{};
        if (top->type != REDIS_REPLY_ARRAY) {
            return redis_error("read response", core::WriteEffect::NotApplicable);
        }
        std::vector<messaging::Delivery> deliveries;
        for (std::size_t stream_index = 0; stream_index < top->elements; ++stream_index) {
            const auto* stream = top->element[stream_index];
            if (!stream || stream->type != REDIS_REPLY_ARRAY || stream->elements != 2 ||
                stream->element[1]->type != REDIS_REPLY_ARRAY) {
                return redis_error("read stream", core::WriteEffect::NotApplicable);
            }
            const auto* entries = stream->element[1];
            for (std::size_t i = 0; i < entries->elements; ++i) {
                const auto* entry = entries->element[i];
                if (!entry || entry->type != REDIS_REPLY_ARRAY || entry->elements != 2 ||
                    entry->element[1]->type != REDIS_REPLY_ARRAY) {
                    return redis_error("read entry", core::WriteEffect::NotApplicable);
                }
                const auto stream_id = reply_text(entry->element[0]);
                const auto* fields = entry->element[1];
                std::string raw_event;
                for (std::size_t field = 0; field + 1 < fields->elements; field += 2) {
                    if (reply_text(fields->element[field]) == "event") {
                        raw_event = reply_text(fields->element[field + 1]);
                        break;
                    }
                }
                messaging::EventEnvelope event;
                bool valid = false;
                try {
                    auto decoded = codec::decode_event(raw_event);
                    if (core::is_ok(decoded)) {
                        event = core::value_of(decoded);
                        valid = true;
                    }
                } catch (...) {
                }
                if (!valid) {
                    event.schema_version = "invalid";
                    event.event_id = core::EventId("invalid-" + stream_id);
                    event.payload_json = raw_event;
                }
                deliveries.push_back({event, {stream_id}, stream_id, 0, raw_event});
            }
        }
        return deliveries;
    }

    RedisStreamOptions options_;
    std::string reclaim_cursor_{"0-0"};
    ContextPtr connection_;
    std::mutex mutex_;
};

}  // namespace

core::Result<std::unique_ptr<messaging::IEventBus>> connect_redis_stream(
    const RedisStreamOptions& options) {
    if (options.stream_key.empty() || options.group.empty()) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "Redis stream key/group");
    }
    auto bus = std::make_unique<RedisStreamBus>(options);
    auto connected = bus->connect();
    if (!core::is_ok(connected)) return core::error_of(connected);
    return std::unique_ptr<messaging::IEventBus>(std::move(bus));
}

}  // namespace ems::storage
