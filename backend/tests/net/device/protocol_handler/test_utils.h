#ifndef EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H
#define EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <thread>
#include <chrono>
#include "../../../../src/net/device/protocol_handler/modbus_handler.h"

namespace ems {
namespace net {
namespace device {
namespace test {

struct TestPoint {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 3;
    std::string decode = "0x41";
    int bit = -1;
};

struct TestPointYX {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 1;
    std::string decode = "bit";
    int bit = 0;
};

struct TestPointYC {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 3;
    std::string decode = "float";
};

struct TestPointYK {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 6;
    int pulse_width = 100;
};

struct TestPointYT {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 6;
    int set_value = 50;
};

class TestModbusServerHelper {
public:
    TestModbusServerHelper() = default;
    ~TestModbusServerHelper();
    
    template<typename PointT>
    void setup_server(ModbusServerHandler<PointT>& handler, int port);
    
    template<typename PointT>
    void teardown_server(ModbusServerHandler<PointT>& handler);
    
private:
    std::vector<std::shared_ptr<void>> servers_;
};

template<typename PointT>
void TestModbusServerHelper::setup_server(ModbusServerHandler<PointT>& handler, int port) {
    std::unordered_map<std::string, ValueType> config;
    config["port"] = port;
    handler.initialize(config);
    
    auto future = handler.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

template<typename PointT>
void TestModbusServerHelper::teardown_server(ModbusServerHandler<PointT>& handler) {
    handler.stop();
}

}
}
}
}

#endif
