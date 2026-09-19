#include <chrono>
#include <iostream>
#include <thread>

#include "rtdb.h"
#include "rtdb_cache.h"

int main() {
    ems::net::RtDbConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.db = 0;
    config.poolSize = 10;
    config.timeoutMs = 5000;
    config.prefix = "ems:";

    auto rtdb = ems::net::createRtDb(config);
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

    ems::net::YcPoint yc;
    yc.code_ = "voltage_a";
    yc.value_ = 220.5;
    yc.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    yc.quality_ = ems::net::PointQuality::Good;

    if (rtdb->setYc("voltage_a", yc)) {
        std::cout << "YC point set successfully" << std::endl;
    }

    ems::net::YcPoint readYc;
    if (rtdb->getYc("voltage_a", readYc)) {
        std::cout << "YC point - Code: " << readYc.code_ << ", Value: " << readYc.value_
                  << ", Quality: " << static_cast<int>(readYc.quality_) << std::endl;
    }

    ems::net::YxPoint yx;
    yx.code_ = "breaker_001";
    yx.value_ = 1;
    yx.timestamp_ = yc.timestamp_;

    if (rtdb->setYx("breaker_001", yx)) {
        std::cout << "YX point set successfully" << std::endl;
    }

    ems::net::YxPoint readYx;
    if (rtdb->getYx("breaker_001", readYx)) {
        std::cout << "YX point - Code: " << readYx.code_ << ", Value: " << readYx.value_
                  << std::endl;
    }

    std::map<std::string, std::string> systemInfo = {
        {"version", "1.0.0"}, {"status", "running"}, {"mode", "auto"}};
    rtdb->setHash("system:info", systemInfo);

    std::map<std::string, std::string> retrievedInfo;
    if (rtdb->getHash("system:info", retrievedInfo)) {
        std::cout << "System Info:" << std::endl;
        for (const auto& item : retrievedInfo) {
            std::cout << "  " << item.first << ": " << item.second << std::endl;
        }
    }

    rtdb->setWithExpire("session:token", "abc123", 3600);

    int64_t counter;
    rtdb->incr("stats:request_count", counter);
    std::cout << "Request count: " << counter << std::endl;

    double floatValue;
    rtdb->incrByFloat("stats:total_power", 10.5, floatValue);
    std::cout << "Total power: " << floatValue << std::endl;

    std::map<std::string, ems::net::YcPoint> batchYc;
    for (int i = 0; i < 5; ++i) {
        ems::net::YcPoint p;
        p.code_ = "voltage_" + std::to_string(i);
        p.value_ = 220.0 + i;
        p.timestamp_ = yc.timestamp_;
        p.quality_ = ems::net::PointQuality::Good;
        batchYc["voltage_" + std::to_string(i)] = p;
    }
    if (rtdb->batchSetYc(batchYc)) {
        std::cout << "Batch set " << batchYc.size() << " YC points with Pipeline" << std::endl;
    }

    std::vector<std::string> batchCodes = {"voltage_0", "voltage_1", "voltage_2"};
    std::vector<ems::net::YcPoint> batchResults;
    if (rtdb->batchGetYc(batchCodes, batchResults)) {
        std::cout << "Batch got " << batchResults.size() << " YC points with Pipeline" << std::endl;
        for (const auto& p : batchResults) {
            std::cout << "  " << p.code_ << ": " << p.value_ << std::endl;
        }
    }

    auto codes = rtdb->getYcCodes("*");
    std::cout << "All YC codes:" << std::endl;
    for (const auto& code : codes) {
        std::cout << "  " << code << std::endl;
    }

    auto asyncResult = rtdb->asyncSet("async_key", "async_value");
    if (asyncResult.get()) {
        std::cout << "Async set succeeded" << std::endl;
    }

    auto asyncGetResult = rtdb->asyncGet("async_key");
    std::cout << "Async got: " << asyncGetResult.get() << std::endl;

    std::cout << "Command count: " << rtdb->getCommandCount() << std::endl;
    std::cout << "Is connected: " << (rtdb->isConnected() ? "yes" : "no") << std::endl;

    rtdb->shutdown();
    std::cout << "RTDB shutdown completed" << std::endl;

    return 0;
}