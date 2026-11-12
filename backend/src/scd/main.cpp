#include <common/core/time.h>
#include <rtdb/client/rtdb.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "common/log/default_logger.hpp"
#include "common/log/process_logger.hpp"
#include "sampling.h"
#include "storage_options.h"

namespace {
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }
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
    ems::storage::ArchiveDirectorySource directory(ems::core::value_of(connected));
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
        LOG_ERROR("ems-storage: TDengine unavailable: {}", ems::core::error_of(opened).message);
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
                LOG_ERROR("ems-storage: TDengine reconnect failed");
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
        if (once) return failed ? 1 : 0;
        // 只执行实际发生的采样；慢周期跳过错过的槽，不能用最新值回填过去。
        do {
            next += interval;
        } while (next <= std::chrono::steady_clock::now());
    }
    realtime.shutdown();
    LOG_INFO("ems-storage stopped");
    return 0;
}
