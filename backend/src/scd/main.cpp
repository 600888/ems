#include <storage/native_adapters.h>
#include <storage/storage.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

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

    ems::storage::TaosConnectionOptions taos;
    taos.host = environment("EMS_TAOS_HOST", taos.host);
    taos.port = port("EMS_TAOS_PORT", taos.port);
    taos.user = environment("EMS_TAOS_USER");
    taos.password = environment("EMS_TAOS_PASSWORD");

    ems::storage::RedisStreamOptions redis;
    redis.host = environment("EMS_REDIS_HOST", redis.host);
    redis.port = port("EMS_REDIS_PORT", redis.port);
    redis.user = environment("EMS_REDIS_USER");
    redis.password = environment("EMS_REDIS_PASSWORD");
    redis.stream_key = environment("EMS_STORAGE_STREAM_KEY");
    const auto scope_id = environment("EMS_STORAGE_SCOPE_ID");
    const auto consumer_id = environment("EMS_STORAGE_CONSUMER_ID");

    if (mysql.port == 0 || taos.port == 0 || redis.port == 0 || mysql.user.empty() ||
        mysql.database.empty() || taos.user.empty() || redis.stream_key.empty() ||
        scope_id.empty() || consumer_id.empty()) {
        std::cerr << "ems-storage: required connection or stream setting is missing\n";
        return 2;
    }

    auto repository_result = ems::storage::connect_mysql_history(mysql);
    auto series_result = ems::storage::connect_taos_series(taos);
    auto bus_result = ems::storage::connect_redis_stream(redis);
    if (!ems::core::is_ok(repository_result) || !ems::core::is_ok(series_result) ||
        !ems::core::is_ok(bus_result)) {
        std::cerr << "ems-storage: failed to connect to MySQL, TDengine or Redis\n";
        return 1;
    }
    auto repository =
        std::move(std::get<std::unique_ptr<ems::storage::IHistoryRepository>>(repository_result));
    auto series =
        std::move(std::get<std::unique_ptr<ems::storage::ITimeSeriesDatabase>>(series_result));
    auto bus = std::move(std::get<std::unique_ptr<ems::messaging::IEventBus>>(bus_result));

    ems::messaging::Subscription subscription;
    subscription.group = redis.group;
    subscription.consumer = consumer_id;
    subscription.scope = scope_id;
    subscription.topic = "telemetry";
    subscription.start = ems::messaging::StartFrom::RetainedBegin;
    ems::storage::HistoryConsumer consumer(*bus, *repository, *series, subscription);
    auto last_reclaim = std::chrono::steady_clock::now() - std::chrono::seconds(30);
    while (running.load()) {
        auto ctx = ems::core::make_call_context(std::chrono::seconds(10));
        if (std::chrono::steady_clock::now() - last_reclaim >= std::chrono::seconds(30)) {
            auto reclaimed = bus->reclaim(subscription, std::chrono::seconds(60), 128, ctx);
            if (!ems::core::is_ok(reclaimed)) {
                std::cerr << "ems-storage: pending reclaim failed\n";
            }
            last_reclaim = std::chrono::steady_clock::now();
        }
        auto batch = consumer.consume_batch(ctx);
        if (!ems::core::is_ok(batch)) {
            std::cerr << "ems-storage: history read failed\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        const auto& report = ems::core::value_of(batch);
        if (report.quarantined || report.deferred) {
            std::cerr << "ems-storage: received=" << report.received
                      << " written=" << report.written << " quarantined=" << report.quarantined
                      << " deferred=" << report.deferred << '\n';
        }
        if (report.deferred) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    bus->close(ems::core::make_call_context(std::chrono::seconds(2)));
    return 0;
}
