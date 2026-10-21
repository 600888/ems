#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "native_adapters.h"
#include "storage.h"
#include "telemetry_feed.h"

namespace {

std::atomic<bool> running{true};

void on_signal(int) { running.store(false); }

std::string environment(const char* name, std::string fallback = {}) {
    const auto* value = std::getenv(name);
    return value ? std::string(value) : std::move(fallback);
}

std::uint16_t port(const char* name, std::uint16_t fallback) {
    const auto value = environment(name);
    if (value.empty()) return fallback;
    try {
        const auto parsed = std::stoul(value);
        if (parsed > 0 && parsed <= 65535) return static_cast<std::uint16_t>(parsed);
    } catch (...) {
    }
    return 0;
}

}  // namespace

int main() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    ems::storage::MySqlConnectionOptions mysql;
    mysql.host = environment("EMS_MYSQL_HOST", mysql.host);
    mysql.port = port("EMS_MYSQL_PORT", mysql.port);
    mysql.user = environment("EMS_MYSQL_USER");
    mysql.password = environment("EMS_MYSQL_PASSWORD");
    mysql.database = environment("EMS_MYSQL_DATABASE");
    mysql.require_history_schema = true;

    ems::storage::TimeSeriesConnectionOptions time_series;
    const auto backend_name = environment("EMS_STORAGE_SERIES_BACKEND", "taos");
    auto backend = ems::storage::parse_time_series_backend(backend_name);
    if (!ems::core::is_ok(backend)) {
        std::cerr << "ems-storage: unknown time-series backend\n";
        return 2;
    }
    time_series.backend = ems::core::value_of(backend);
    auto& taos = time_series.taos;
    taos.host = environment("EMS_TAOS_HOST", taos.host);
    taos.port = port("EMS_TAOS_PORT", taos.port);
    taos.user = environment("EMS_TAOS_USER");
    taos.password = environment("EMS_TAOS_PASSWORD");

    const auto scope_kind = environment("EMS_STORAGE_SCOPE_KIND");
    const auto scope_id = environment("EMS_STORAGE_SCOPE_ID");
    ems::storage::ZmqTelemetryOptions telemetry;
    telemetry.endpoint = environment("EMS_STORAGE_ZMQ_ENDPOINT", telemetry.endpoint);
    telemetry.topic =
        environment("EMS_STORAGE_ZMQ_TOPIC", "ems:telemetry:" + scope_kind + ":" + scope_id);

    if (mysql.port == 0 || mysql.user.empty() || mysql.database.empty() || scope_id.empty() ||
        (scope_kind != "station" && scope_kind != "unit") || telemetry.endpoint.empty() ||
        telemetry.topic.empty() ||
        (time_series.backend == ems::storage::TimeSeriesBackend::Tdengine &&
         (taos.port == 0 || taos.user.empty()))) {
        std::cerr << "ems-storage: required connection or telemetry setting is missing\n";
        return 2;
    }

    auto series_result = ems::storage::connect_time_series(time_series);
    if (!ems::core::is_ok(series_result)) {
        std::cerr << "ems-storage: time-series backend: "
                  << ems::core::error_of(series_result).message << '\n';
        return 1;
    }
    auto repository_result = ems::database::connect_mysql_database(
        mysql, ems::core::make_call_context(std::chrono::seconds(10)));
    if (!ems::core::is_ok(repository_result)) {
        std::cerr << "ems-storage: MySQL history repository: "
                  << ems::core::error_of(repository_result).message << '\n';
        return 1;
    }
    auto feed_result = ems::storage::connect_zmq_telemetry(telemetry);
    if (!ems::core::is_ok(feed_result)) {
        std::cerr << "ems-storage: failed to subscribe to ZeroMQ telemetry\n";
        return 1;
    }
    auto database = std::move(std::get<ems::database::MySqlDatabaseContext>(repository_result));
    auto series =
        std::move(std::get<std::unique_ptr<ems::storage::ITimeSeriesDatabase>>(series_result));
    auto feed = std::move(std::get<std::unique_ptr<ems::storage::IHistoryFeed>>(feed_result));

    ems::storage::ConsumerOptions consumer_options;
    consumer_options.scope_kind = scope_kind;
    ems::storage::HistoryConsumer consumer(*feed, *database.database, *database.history_catalog,
                                           *database.history_events, *series, scope_id,
                                           consumer_options);
    std::uint64_t last_local_drop_count = 0;
    while (running.load()) {
        auto ctx = ems::core::make_call_context(std::chrono::seconds(10));
        auto batch = consumer.consume_batch(ctx);
        if (!ems::core::is_ok(batch)) {
            std::cerr << "ems-storage: telemetry read failed\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        const auto& report = ems::core::value_of(batch);
        const auto stats = feed->stats();
        if (stats.dropped_local != last_local_drop_count) {
            std::cerr << "ems-storage: local telemetry queue dropped "
                      << stats.dropped_local - last_local_drop_count << " samples\n";
            last_local_drop_count = stats.dropped_local;
        }
        if (report.quarantined || report.dropped) {
            std::cerr << "ems-storage: received=" << report.received
                      << " written=" << report.written << " quarantined=" << report.quarantined
                      << " dropped=" << report.dropped << '\n';
        }
    }
    feed->close();
    const auto final_stats = feed->stats();
    if (final_stats.dropped_local != last_local_drop_count) {
        std::cerr << "ems-storage: local telemetry queue dropped "
                  << final_stats.dropped_local - last_local_drop_count << " samples on shutdown\n";
    }
    return 0;
}
