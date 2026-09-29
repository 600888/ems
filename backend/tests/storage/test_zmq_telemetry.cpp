#include <common/codec/event_codec.h>
#include <common/core/identity.h>
#include <common/messaging/zmq_change_channel.h>

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "telemetry_feed.h"

namespace {
ems::messaging::ZmqChangeSubscriber::Callback receiver;
}

namespace ems::messaging {

struct ZmqChangeSubscriber::Impl {};

ZmqChangeSubscriber::ZmqChangeSubscriber() : impl_(std::make_unique<Impl>()) {}

ZmqChangeSubscriber::~ZmqChangeSubscriber() { stop(); }

bool ZmqChangeSubscriber::start(const ZmqChangeConfig&, const std::vector<std::string>&,
                                Callback callback) {
    receiver = std::move(callback);
    return true;
}

void ZmqChangeSubscriber::stop() noexcept { receiver = {}; }

}  // namespace ems::messaging

int main() {
    ems::storage::ZmqTelemetryOptions options;
    options.topic = "ems:telemetry:station-1";
    options.max_queue_items = 1;
    auto connected = ems::storage::connect_zmq_telemetry(options);
    assert(ems::core::is_ok(connected));
    auto feed = std::move(std::get<std::unique_ptr<ems::storage::IHistoryFeed>>(connected));

    ems::messaging::EventEnvelope event;
    event.schema_version = "1";
    event.event_type = ems::messaging::EventType::Telemetry;
    event.event_id = ems::core::EventId("event-1");
    event.producer_id = "collector-1";
    event.station_id = ems::core::StationId("station-1");
    event.config_version = "7";
    event.event_time = 1000000;
    event.ingest_time = 1000000;
    event.payload_json = "{\"point_id\":\"point-1\"}";
    auto encoded = ems::codec::encode_event(event);
    assert(ems::core::is_ok(encoded));
    const auto raw = ems::core::value_of(encoded);

    receiver(options.topic, raw);
    receiver(options.topic, raw);
    auto stats = feed->stats();
    assert(stats.received == 2);
    assert(stats.dropped_local == 1);
    auto batch =
        feed->read(128, 1024 * 1024, ems::core::make_call_context(std::chrono::seconds(1)));
    assert(ems::core::is_ok(batch));
    assert(ems::core::value_of(batch).size() == 1);
    assert(ems::core::value_of(batch).front().event.event_id.value() == "event-1");

    receiver(options.topic, "{malformed");
    auto malformed =
        feed->read(1, 1024 * 1024, ems::core::make_call_context(std::chrono::seconds(1)));
    assert(ems::core::is_ok(malformed));
    assert(ems::core::value_of(malformed).front().event.schema_version == "invalid");
    assert(ems::core::value_of(malformed).front().raw_payload == "{malformed");
    receiver(options.topic, raw);
    auto over_budget = feed->read(1, 1, ems::core::make_call_context(std::chrono::seconds(1)));
    assert(ems::core::is_ok(over_budget));
    assert(ems::core::value_of(over_budget).empty());
    assert(feed->stats().dropped_local == 2);
    receiver(options.topic, raw);
    feed->close();
    assert(feed->stats().dropped_local == 3);
    return 0;
}
