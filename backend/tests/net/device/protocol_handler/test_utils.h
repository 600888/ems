#ifndef EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H
#define EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../../../src/net/protocol/modbus/modbus_handler.h"

namespace ems {
namespace net {
namespace device {
namespace test {

class TestModbusServerHelper {
   public:
    TestModbusServerHelper() = default;
    ~TestModbusServerHelper();

    void setup_server(ModbusServerHandler& handler, int port);

    void teardown_server(ModbusServerHandler& handler);

   private:
    std::vector<std::shared_ptr<void>> servers_;
};

inline void TestModbusServerHelper::setup_server(ModbusServerHandler& handler, int port) {
    std::unordered_map<std::string, ValueType> config;
    config["port"] = port;
    handler.initialize(config);

    auto future = handler.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

inline void TestModbusServerHelper::teardown_server(ModbusServerHandler& handler) {
    handler.stop();
}

}  // namespace test
}  // namespace device
}  // namespace net
}  // namespace ems

#endif
