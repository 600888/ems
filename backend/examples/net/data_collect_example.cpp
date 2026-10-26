/** @file data_collect_example.cpp
 * @brief 从新设备表启动采集；参数为 MySQL JSON、实时库 JSON、插件目录与站编码。
 */
#include <common/database/mysql_database.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "net/collect/collect_engine.h"
#include "net/para_loader/cfg_loader.h"

namespace {
volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }
}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr
            << "Usage: data_collect_example database.json realtime.json plugin_dir scope_code\n";
        return 2;
    }
    auto options = ems::database::load_mysql_options(argv[1]);
    if (!ems::core::is_ok(options)) return 1;
    auto mysql = ems::core::value_of(options);
    mysql.require_configuration_schema = true;
    auto database = ems::database::connect_mysql_database(
        mysql, ems::core::make_call_context(std::chrono::seconds(10)));
    if (!ems::core::is_ok(database)) return 1;
    ems::net::device::CfgLoader loader;
    if (!loader.initialize(ems::core::value_of(database),
                           ems::core::make_call_context(std::chrono::seconds(10))))
        return 1;
    ems::net::collect::EngineConfig config;
    config.rtdbConfigPath_ = argv[2];
    config.pluginDir_ = argv[3];
    config.rtdbScopeCode_ = argv[4];
    config.notificationBackend_ = ems::net::collect::NotificationBackend::None;
    ems::net::collect::CollectEngine engine;
    if (!engine.initialize(config)) return 1;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    if (engine.start_all(loader) != static_cast<int>(loader.channels().size())) return 1;
    while (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.shutdown();
}
