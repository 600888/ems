#include <rtdb/client/rtdb_ems.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    ems::rtdb::RtDbConfig config;
    config.config_path = argv[1];
    auto db = ems::rtdb::createRtdbEms(config);
    if (!db) return 3;
    ems::rtdb::DeviceSnapshot snapshot;
    snapshot.DeviceType = "PCS";
    snapshot.ycValues["power"] = 42;
    if (!db->setDeviceSnapshot("pcs01", snapshot)) return 4;
    ems::rtdb::DeviceSnapshot read;
    if (!db->getDeviceSnapshot("pcs01", read)) return 5;
    std::cout << read.device_id << ": " << read.ycValues.at("power") << '\n';
}
