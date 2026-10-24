#include <rtdb/client/rtdb.h>

#include <iostream>

// Start ems-rtdb with the same configuration first, then run write/read in separate processes.
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    ems::rtdb::RtDbConfig config;
    config.config_path = argv[1];
    auto db = ems::rtdb::createRtDb(config);
    if (!db) return 3;
    if (std::string(argv[2]) == "write") return db->set("demo:status", "ready") ? 0 : 4;
    if (std::string(argv[2]) != "read") return 2;
    std::string value;
    if (!db->get("demo:status", value)) return 5;
    std::cout << value << '\n';
}
