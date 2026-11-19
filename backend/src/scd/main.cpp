#include <common/core/time.h>
#include <rtdb/client/rtdb.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <future>
#include <iostream>
#include <thread>

#include "alarm_archiver.h"
#include "common/log/default_logger.hpp"
#include "common/log/process_logger.hpp"
#include "sampling.h"
#include "storage_options.h"

namespace {
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }

class AlarmArchiveRunner {
   public:
    AlarmArchiveRunner(const ems::database::MySqlDatabaseContext& database,
                       const ems::database::TimeSeriesConnectionOptions& options,
                       ems::core::StationId station_id)
        : database_(database), options_(options), station_id_(std::move(station_id)) {}

    bool poll() {
        if (!series_) {
            auto opened = ems::database::connect_time_series(options_);
            if (!ems::core::is_ok(opened)) return report(ems::core::error_of(opened));
            auto series = std::get<0>(std::move(opened));
            const auto checked =
                series->check_alarm_schema(ems::core::make_call_context(std::chrono::seconds(5)));
            if (!ems::core::is_ok(checked)) return report(ems::core::error_of(checked));
            series_ = std::move(series);
        }

        const auto archived = ems::storage::archive_alarm_batch(
            *database_.database, *database_.alarms, *series_, station_id_,
            ems::core::make_call_context(std::chrono::seconds(5)));
        if (!ems::core::is_ok(archived)) {
            const auto& error = ems::core::error_of(archived);
            if (error.code == ems::core::ErrorCode::Unavailable) series_.reset();
            return report(error);
        }
        const auto count = ems::core::value_of(archived);
        if (count != 0) LOG_INFO("ems-storage alarm archive: written={}", count);
        return true;
    }

   private:
    static bool report(const ems::core::Error& error) {
        LOG_WARN("ems-storage alarm archive deferred: code={}, effect={}, reason={}",
                 ems::core::to_string(error.code), ems::core::to_string(error.effect),
                 error.message);
        return false;
    }

    const ems::database::MySqlDatabaseContext& database_;
    const ems::database::TimeSeriesConnectionOptions& options_;
    ems::core::StationId station_id_;
    std::unique_ptr<ems::database::ITimeSeriesDatabase> series_;
};
}  // namespace

int main(int argc, char** argv) {
    const ProcessLogger logger("ems-storage");
    std::string config = "conf/storage.json";
    bool check = false, once = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "ems-storage [--config path] [--check-config] [--once]\n";
            return 0;
        }
        if (argument == "--config" && i + 1 < argc)
            config = argv[++i];
        else if (argument == "--check-config")
            check = true;
        else if (argument == "--once")
            once = true;
        else {
            std::cerr << "ems-storage: invalid arguments; use --help\n";
            return 2;
        }
    }
    auto loaded = ems::storage::load_storage_options(config);
    if (!ems::core::is_ok(loaded)) {
        std::cerr << "ems-storage: invalid connection or sampling configuration\n";
        return 2;
    }
    const auto options = ems::core::value_of(loaded);
    if (check) {
        std::cout << "Storage configuration valid; interval_ms=" << options.interval_ms
                  << " (no external connections opened)\n";
        return 0;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
#ifdef _WIN32
    std::signal(SIGBREAK, stop);
#endif
    auto connected = ems::database::connect_mysql_database(
        options.database, ems::core::make_call_context(std::chrono::seconds(10)));
    if (!ems::core::is_ok(connected)) {
        LOG_ERROR("ems-storage: configuration database unavailable");
        return 1;
    }
    const auto& database = ems::core::value_of(connected);
    ems::storage::ArchiveDirectorySource directory(database);
    ems::rtdb::RtDb realtime;
    ems::rtdb::RtDbConfig realtime_options;
    realtime_options.config_path = options.realtime_config;
    realtime_options.scope_code = options.scope_code;
    if (!realtime.initialize(realtime_options)) {
        LOG_ERROR("ems-storage: realtime backend unavailable");
        return 1;
    }
    auto opened = ems::database::connect_time_series(options.timeseries);
    if (!ems::core::is_ok(opened)) {
        LOG_ERROR("ems-storage: time-series backend unavailable: {}",
                  ems::core::error_of(opened).message);
        return 1;
    }
    auto series = std::move(std::get<0>(opened));
    const auto reader = [&](const auto& type, const auto& id, const auto& ctx) {
        return realtime.readPointJson(type, id, ctx);
    };
    auto sampler = std::make_unique<ems::storage::PeriodicArchiveSampler>(reader, *series);
    auto initial = directory.refresh(ems::core::make_call_context(std::chrono::seconds(30)));
    if (!ems::core::is_ok(initial)) {
        LOG_ERROR("ems-storage: archive directory unavailable: {}",
                  ems::core::error_of(initial).message);
        return 1;
    }
    AlarmArchiveRunner alarm_runner(database, options.timeseries,
                                    ems::core::StationId(ems::core::value_of(initial).station_id));
    // 停止信号先于 worker 析构，即使提前退出也会等待后台归档结束。
    std::future<void> alarm_worker;
    std::promise<void> stop_alarm_worker;
    if (options.archive_alarms && !once) {
        alarm_worker = std::async(
            std::launch::async, [&alarm_runner, stop_signal = stop_alarm_worker.get_future()] {
                auto delay = std::chrono::seconds(1);
                do {
                    if (alarm_runner.poll())
                        delay = std::chrono::seconds(1);
                    else
                        delay = std::min(delay * 2, std::chrono::seconds(30));
                } while (stop_signal.wait_for(delay) == std::future_status::timeout);
            });
    }
    LOG_INFO("ems-storage ready: periodic archive, interval_ms={}", options.interval_ms);
    const auto interval = std::chrono::milliseconds(options.interval_ms);
    auto next = std::chrono::steady_clock::now();
    bool reconnect = false;
    while (!stopping) {
        if (std::chrono::steady_clock::now() < next) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        const auto cycle_started = std::chrono::steady_clock::now();
        auto ctx = ems::core::make_call_context(interval);
        if (reconnect) {
            auto retry = ems::database::connect_time_series(options.timeseries);
            if (!ems::core::is_ok(retry)) {
                LOG_ERROR("ems-storage: time-series reconnect failed");
                next = std::chrono::steady_clock::now() + interval;
                continue;
            }
            sampler.reset();
            series = std::move(std::get<0>(retry));
            sampler = std::make_unique<ems::storage::PeriodicArchiveSampler>(reader, *series);
            reconnect = false;
        }
        auto current = directory.refresh(ctx);
        bool failed = false;
        if (!ems::core::is_ok(current)) {
            failed = true;
            LOG_ERROR("ems-storage: directory refresh failed: {}",
                      ems::core::error_of(current).message);
        } else {
            const auto& points = ems::core::value_of(current);
            const auto span_us = options.interval_ms * 1000;
            const auto slot = (ems::core::now_utc_micros() / span_us) * span_us;
            const auto report = sampler->sample(points, slot, ctx);
            failed = report.failed != 0;
            reconnect = failed;
            LOG_INFO(
                "ems-storage cycle: points={} written={} invalid={} failed={} revision={} "
                "elapsed_ms={}",
                points.points.size(), report.written, report.invalid, report.failed,
                points.config_revision,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - cycle_started)
                    .count());
        }
        if (once) {
            const bool alarm_ok = !options.archive_alarms || alarm_runner.poll();
            return failed || !alarm_ok ? 1 : 0;
        }
        // 只执行实际发生的采样；慢周期跳过错过的槽，不能用最新值回填过去。
        do {
            next += interval;
        } while (next <= std::chrono::steady_clock::now());
    }
    stop_alarm_worker.set_value();
    if (alarm_worker.valid()) alarm_worker.get();
    realtime.shutdown();
    LOG_INFO("ems-storage stopped");
    return 0;
}
