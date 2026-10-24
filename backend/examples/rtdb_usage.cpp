#include <rtdb/client/rtdb.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    ems::rtdb::RtDbConfig config;
    config.config_path = argv[1];
    auto db = ems::rtdb::createRtDb(config);
    if (!db) return 3;
    ems::rtdb::YcPoint point;
    point.value_ = 42.5;
    point.unit_ = "kW";
    if (!db->setYc("pcs01.power", point)) return 4;
    ems::rtdb::YcPoint read;
    if (!db->getYc("pcs01.power", read)) return 5;
    std::cout << read.code_ << ": " << read.value_ << ' ' << read.unit_ << '\n';
}
