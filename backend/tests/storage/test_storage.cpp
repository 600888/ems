#include <common/core/identity.h>
#include <storage/outbox_publisher.h>
#include <storage/storage.h>

#include <cassert>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ems::core::CallContext;
using ems::core::ErrorCode;
using ems::core::Result;
using ems::core::Status;
using ems::core::WriteEffect;
using ems::messaging::Delivery;
using ems::messaging::EventEnvelope;
using ems::messaging::IEventBus;
using ems::messaging::PublishReceipt;
using ems::messaging::Subscription;
using ems::storage::IHistoryRepository;
using ems::storage::ITimeSeriesReader;
using ems::storage::ITimeSeriesStore;
using ems::storage::PointSnapshot;
using ems::storage::QuarantineRecord;
using ems::storage::Sample;
using ems::storage::ValueType;

struct FakeBus final : IEventBus {
    std::vector<Delivery> deliveries;
    std::vector<std::string> acked;

    Result<PublishReceipt> publish(const std::string&, const std::string&, const EventEnvelope&,
                                   const CallContext&) override {
        return PublishReceipt{};
    }

    Result<std::vector<Delivery>> read(const Subscription&, std::uint64_t, std::uint64_t,
                                       const CallContext&) override {
        return deliveries;
    }

    Status ack(const Subscription&, const std::string& token, const CallContext&) override {
        acked.push_back(token);
        return ems::core::ok_status();
    }

    Result<std::uint64_t> reclaim(const Subscription&, std::chrono::milliseconds, std::uint64_t,
                                  const CallContext&) override {
        return std::uint64_t{0};
    }

    Status close(const CallContext&) override { return ems::core::ok_status(); }
};

struct FakeRepository final : IHistoryRepository {
    std::map<std::string, std::string> identities;
    std::vector<QuarantineRecord> quarantined;
    bool ready{true};
    bool quarantine_available{true};

    Result<PointSnapshot> find_point(const std::string& kind, const std::string& scope,
                                     const std::string& version, const std::string& point,
                                     const CallContext&) override {
        if (!ready) {
            return ems::core::make_error(ErrorCode::NotReady, WriteEffect::NotApplicable,
                                         "snapshot pending");
        }
        return PointSnapshot{
            kind,    scope, version, point, "asset-1", "p1", "Power", "kW", ValueType::Integer,
            "p_123", true};
    }

    Result<ems::storage::SeriesMapping> find_series(const std::string& point,
                                                    const CallContext&) override {
        return ems::storage::SeriesMapping{point, ValueType::Integer, "p_123"};
    }

    Status register_event(const Sample& sample, const CallContext&) override {
        auto [item, inserted] = identities.emplace(sample.event_id, sample.payload_sha256);
        if (!inserted && item->second != sample.payload_sha256) {
            return ems::core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied,
                                         "event identity conflict");
        }
        return ems::core::ok_status();
    }

    Status quarantine(const QuarantineRecord& record, const CallContext&) override {
        if (!quarantine_available) {
            return ems::core::make_error(ErrorCode::Unavailable, WriteEffect::Unknown,
                                         "quarantine unavailable");
        }
        quarantined.push_back(record);
        return ems::core::ok_status();
    }
};

struct FakeSeries final : ITimeSeriesStore {
    std::vector<Sample> written;
    bool available{true};

    Status write(const Sample& sample, const CallContext&) override {
        if (!available) {
            return ems::core::make_error(ErrorCode::Unavailable, WriteEffect::Unknown,
                                         "TAOS unavailable");
        }
        written.push_back(sample);
        return ems::core::ok_status();
    }
};

struct FakeReader final : ITimeSeriesReader {
    std::vector<Sample> samples;

    Result<std::vector<Sample>> scan(const ems::storage::HistoryQuery&,
                                     const std::vector<ems::storage::SeriesMapping>&, std::uint32_t,
                                     const CallContext&) override {
        return samples;
    }
};

struct FakeTransaction final : ems::database::ITransaction {
    bool* fail_commit;

    explicit FakeTransaction(bool* failure) : fail_commit(failure) {}

    Result<ems::database::CommitReceipt> commit(const CallContext&) override {
        if (*fail_commit) {
            return ems::core::make_error(ErrorCode::Unavailable, WriteEffect::Unknown,
                                         "commit response unknown");
        }
        return ems::database::CommitReceipt{};
    }

    Status rollback(const CallContext&) override { return ems::core::ok_status(); }
};

struct FakeDatabase final : ems::database::IDatabase {
    bool fail_commit{false};

    Result<std::unique_ptr<ems::database::ITransaction>> begin(const CallContext&) override {
        return std::unique_ptr<ems::database::ITransaction>(new FakeTransaction(&fail_commit));
    }
};

struct FakeOutbox final : ems::database::IOutboxRepository {
    std::uint32_t marked{0};

    Status append(ems::database::ITransaction&, const ems::database::OutboxRecord&,
                  const CallContext&) override {
        return ems::core::ok_status();
    }

    Result<std::vector<ems::database::ClaimedOutboxItem>> claim(ems::database::ITransaction&,
                                                                const std::string&, std::int64_t,
                                                                std::uint32_t,
                                                                const CallContext&) override {
        ems::database::ClaimedOutboxItem item;
        item.outbox_id = 42;
        item.record.event_id = "stable-event";
        item.claim_token = "lease-1";
        return std::vector<ems::database::ClaimedOutboxItem>{item};
    }

    Status mark_published(ems::database::ITransaction&, std::int64_t, const std::string&,
                          const CallContext&) override {
        ++marked;
        return ems::core::ok_status();
    }

    Result<std::int64_t> oldest_pending_age_ms(ems::database::ITransaction&,
                                               const CallContext&) override {
        return std::int64_t{0};
    }
};

struct FakeSink final : ems::storage::IOutboxSink {
    bool available{false};
    std::uint32_t attempts{0};

    Status publish(const ems::database::ClaimedOutboxItem&, const CallContext&) override {
        ++attempts;
        if (!available) {
            return ems::core::make_error(ErrorCode::Unavailable, WriteEffect::Unknown,
                                         "publish response unknown");
        }
        return ems::core::ok_status();
    }
};

Delivery make_delivery(std::string id, std::string value = "9223372036854775807") {
    EventEnvelope event;
    event.schema_version = "1";
    event.event_type = ems::messaging::EventType::Telemetry;
    event.event_id = ems::core::EventId(std::move(id));
    event.producer_id = "collector-1";
    event.station_id = ems::core::StationId("station-1");
    event.config_version = "7";
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    event.event_time = now;
    event.ingest_time = now;
    event.payload_json =
        "{\"point_id\":\"point-1\",\"source_id\":\"meter-1\","
        "\"source_epoch\":\"4\",\"sequence\":\"9\","
        "\"value_type\":\"integer\",\"value\":\"" +
        value + "\",\"quality\":0}";
    return Delivery{event, {event.event_id.value()}, event.event_id.value(), 1};
}

ems::storage::HistoryConsumer make_consumer(FakeBus& bus, FakeRepository& repository,
                                            FakeSeries& series) {
    Subscription subscription;
    subscription.group = "history-storage";
    subscription.consumer = "node-1";
    subscription.scope = "station-1";
    subscription.topic = "telemetry";
    return {bus, repository, series, subscription};
}

}  // namespace

int main() {
    FakeBus bus;
    FakeRepository repository;
    FakeSeries series;
    auto consumer = make_consumer(bus, repository, series);
    const auto ctx = ems::core::make_call_context(std::chrono::seconds(10));

    bus.deliveries = {make_delivery("event-1")};
    series.available = false;
    auto first = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(first));
    assert(ems::core::value_of(first).deferred == 1);
    assert(bus.acked.empty());
    assert(repository.identities.size() == 1);

    series.available = true;
    auto retry = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(retry));
    assert(ems::core::value_of(retry).written == 1);
    assert(series.written.size() == 1);
    assert(std::get<std::int64_t>(series.written.front().value) == INT64_MAX);
    assert(bus.acked.size() == 1);

    bus.deliveries = {make_delivery("event-1", "1")};
    auto conflict = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(conflict));
    assert(ems::core::value_of(conflict).quarantined == 1);
    assert(series.written.size() == 1);
    assert(repository.quarantined.size() == 1);

    bus.deliveries = {make_delivery("event-2")};
    repository.ready = false;
    auto pending = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(pending));
    assert(ems::core::value_of(pending).deferred == 1);
    assert(bus.acked.size() == 2);

    repository.ready = true;
    repository.quarantine_available = false;
    bus.deliveries = {make_delivery("event-3", "not-an-integer")};
    auto failed_quarantine = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(failed_quarantine));
    assert(ems::core::value_of(failed_quarantine).deferred == 1);
    assert(bus.acked.size() == 2);

    repository.quarantine_available = true;
    auto malformed = make_delivery("event-malformed");
    malformed.event.schema_version = "invalid";
    malformed.raw_payload = "{malformed original event";
    bus.deliveries = {malformed};
    auto isolated = consumer.consume_batch(ctx);
    assert(ems::core::is_ok(isolated));
    assert(ems::core::value_of(isolated).quarantined == 1);
    assert(repository.quarantined.back().payload == malformed.raw_payload);

    FakeReader reader;
    auto first_row = series.written.front();
    auto second_row = first_row;
    second_row.event_id = "event-4";
    reader.samples = {first_row, second_row};
    ems::storage::HistoryQueryService query_service(repository, reader);
    ems::storage::HistoryQuery query;
    query.scope_kind = "station";
    query.scope_id = "station-1";
    query.point_ids = {"point-1"};
    query.start_us = first_row.source_time_us - 1;
    query.end_us = first_row.source_time_us + 1;
    query.max_rows = 1;
    auto denied = query_service.query(query, {}, ctx);
    assert(!ems::core::is_ok(denied));
    assert(ems::core::error_of(denied).code == ErrorCode::PermissionDenied);
    auto page = query_service.query(query, {"station:station-1"}, ctx);
    assert(ems::core::is_ok(page));
    assert(ems::core::value_of(page).rows.size() == 1);
    assert(ems::core::value_of(page).next.has_value());
    assert(ems::core::value_of(page).rows.front().point.display_name == "Power");

    FakeDatabase database;
    FakeOutbox outbox;
    FakeSink sink;
    ems::storage::OutboxPublisher publisher(database, outbox, sink, "storage-1", 30000, 16);
    auto not_sent = publisher.publish_batch(ctx);
    assert(ems::core::is_ok(not_sent));
    assert(ems::core::value_of(not_sent).deferred == 1);
    assert(outbox.marked == 0);
    sink.available = true;
    auto sent = publisher.publish_batch(ctx);
    assert(ems::core::is_ok(sent));
    assert(ems::core::value_of(sent).published == 1);
    assert(outbox.marked == 1);
    return 0;
}
