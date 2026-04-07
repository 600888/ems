#include "rtdb.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    ems::net::RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "";
    config.db = 0;
    config.pool_size = 10;
    config.timeout_ms = 5000;
    config.prefix = "ems:";

    auto rtdb = ems::net::createRTDB(config);
    if (!rtdb) {
        std::cerr << "Failed to create RTDB" << std::endl;
        return 1;
    }

    std::cout << "RTDB initialized successfully" << std::endl;

    rtdb->set("test_key", "hello_world");
    std::string value;
    if (rtdb->get("test_key", value)) {
        std::cout << "Got value: " << value << std::endl;
    }

    rtdb->setDataPoint("sensor/temperature", "25.5", 0);
    rtdb->setDataPoint("sensor/humidity", "60.0", 0);
    rtdb->setDataPoint("sensor/pressure", "101.325", 0);

    ems::net::DataPoint point;
    if (rtdb->getDataPoint("sensor/temperature", point)) {
        std::cout << "DataPoint - Key: " << point.key
                  << ", Value: " << point.value
                  << ", Timestamp: " << point.timestamp
                  << ", Quality: " << point.quality << std::endl;
    }

    auto points = rtdb->getDataPoints("sensor/*");
    std::cout << "Found " << points.size() << " sensor data points" << std::endl;

    std::map<std::string, std::string> system_info = {
        {"version", "1.0.0"},
        {"status", "running"},
        {"mode", "auto"}
    };
    rtdb->setHash("system:info", system_info);

    std::map<std::string, std::string> retrieved_info;
    if (rtdb->getHash("system:info", retrieved_info)) {
        std::cout << "System Info:" << std::endl;
        for (const auto& item : retrieved_info) {
            std::cout << "  " << item.first << ": " << item.second << std::endl;
        }
    }

    rtdb->setWithExpire("session:token", "abc123", 3600);

    int64_t counter;
    rtdb->incr("stats:request_count", counter);
    std::cout << "Request count: " << counter << std::endl;

    double float_value;
    rtdb->incrByFloat("stats:total_power", 10.5, float_value);
    std::cout << "Total power: " << float_value << std::endl;

    auto keys = rtdb->keys("*");
    std::cout << "All keys:" << std::endl;
    for (const auto& key : keys) {
        std::cout << "  " << key << std::endl;
    }

    std::cout << "Is connected: " << (rtdb->isConnected() ? "yes" : "no") << std::endl;

    rtdb->shutdown();
    std::cout << "RTDB shutdown completed" << std::endl;

    return 0;
}
