#include <chrono>
#include <iostream>
#include <thread>

#include "rtdb/rtdb.h"

using namespace ems::net;

void example_yc() {
    std::cout << "\n=== 遥测YC示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    config.timeout_ms = 5000;

    auto rtdb = createRTDB(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        if (rtdb) {
            std::cerr << "Error: " << rtdb->getLastError() << std::endl;
        }
        return;
    }

    YCPoint yc_point;
    yc_point.value = 220.5;
    yc_point.timestamp = std::time(nullptr);
    yc_point.quality = static_cast<int>(PointQuality::Good);
    yc_point.status = 0;

    if (rtdb->setYC("voltage_a", yc_point)) {
        std::cout << "遥测点voltage_a写入成功: " << yc_point.value << std::endl;
    }

    YCPoint read_point;
    if (rtdb->getYC("voltage_a", read_point)) {
        std::cout << "遥测点voltage_a读取成功: " << read_point.value
                  << ", quality: " << read_point.quality << std::endl;
    }

    std::cout << "所有YC测点: ";
    auto codes = rtdb->getYCCodes();
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;
}

void example_yx() {
    std::cout << "\n=== 遥信YX示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDB(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return;
    }

    YXPoint yx_point;
    yx_point.value = 1;
    yx_point.timestamp = std::time(nullptr);
    yx_point.quality = static_cast<int>(PointQuality::Good);
    yx_point.status = 0;

    if (rtdb->setYX("breaker_001", yx_point)) {
        std::cout << "遥信点breaker_001写入成功: " << yx_point.value << std::endl;
    }

    YXPoint read_point;
    if (rtdb->getYX("breaker_001", read_point)) {
        std::cout << "遥信点breaker_001读取成功: " << read_point.value << std::endl;
    }

    std::cout << "所有YX测点: ";
    auto codes = rtdb->getYXCodes();
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;
}

void example_yk() {
    std::cout << "\n=== 遥控YK示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDB(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return;
    }

    YKPoint yk_point;
    yk_point.value = 1;
    yk_point.command_type = 0;
    yk_point.timestamp = std::time(nullptr);
    yk_point.status = YKStatus::Idle;
    yk_point.result = 0;

    if (rtdb->setYK("yk_breaker_001", yk_point)) {
        std::cout << "遥控点yk_breaker_001写入成功" << std::endl;
    }

    YKPoint read_point;
    if (rtdb->getYK("yk_breaker_001", read_point)) {
        std::cout << "遥控点yk_breaker_001读取成功, status: " << static_cast<int>(read_point.status)
                  << std::endl;
    }

    std::cout << "所有YK测点: ";
    auto codes = rtdb->getYKCodes();
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;
}

void example_yt() {
    std::cout << "\n=== 遥调YT示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDB(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return;
    }

    YTPoint yt_point;
    yt_point.value = 50.0;
    yt_point.timestamp = std::time(nullptr);
    yt_point.quality = static_cast<int>(PointQuality::Good);
    yt_point.status = 0;

    if (rtdb->setYT("setpoint_001", yt_point)) {
        std::cout << "遥调点setpoint_001写入成功: " << yt_point.value << std::endl;
    }

    YTPoint read_point;
    if (rtdb->getYT("setpoint_001", read_point)) {
        std::cout << "遥调点setpoint_001读取成功: " << read_point.value << std::endl;
    }

    std::cout << "所有YT测点: ";
    auto codes = rtdb->getYTCodes();
    for (const auto& code : codes) {
        std::cout << code << " ";
    }
    std::cout << std::endl;
}

void example_batch_operations() {
    std::cout << "\n=== 批量操作示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDB(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return;
    }

    auto yc_codes = rtdb->getYCCodes("voltage_*");
    std::cout << "匹配voltage_*的YC测点数量: " << yc_codes.size() << std::endl;

    auto yx_codes = rtdb->getYXCodes("breaker_*");
    std::cout << "匹配breaker_*的YX测点数量: " << yx_codes.size() << std::endl;

    auto all_yc = rtdb->getYCPoints("*");
    std::cout << "所有YC测点数量: " << all_yc.size() << std::endl;

    for (const auto& point : all_yc) {
        std::cout << "  - value: " << point.value << ", timestamp: " << point.timestamp
                  << ", quality: " << point.quality << std::endl;
    }
}

int main() {
    std::cout << "EMS RTDB 使用示例" << std::endl;
    std::cout << "=================" << std::endl;

    example_yc();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    example_yx();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    example_yk();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    example_yt();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    example_batch_operations();

    std::cout << "\n示例完成!" << std::endl;
    return 0;
}
