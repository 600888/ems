#include <rtdb/client/rtdb_manager.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    ems::rtdb::RtDbConfig config;
    config.config_path = argv[1];
    ems::rtdb::RtDbManager manager;
    if (!manager.initialize(config)) return 3;
    ems::rtdb::RtDbConfigCache::YcConfig pointConfig;
    pointConfig.code = "power";
    pointConfig.name = "Active power";
    manager.getConfigCache()->loadYcConfigs({pointConfig});
    ems::rtdb::YcPoint value;
    value.value_ = 42;
    if (!manager.updateYc("power", value)) return 4;
    ems::rtdb::RtDbPointManager::YcFullPoint full;
    if (!manager.getYcFull("power", full)) return 5;
    std::cout << full.config.name << ": " << full.data.value_ << '\n';
}
