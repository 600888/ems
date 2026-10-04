#ifndef EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H
#define EMS_TESTS_NET_DEVICE_PROTOCOL_HANDLER_TEST_UTILS_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../../../src/net/handler/modbus/modbus_handler.h"

namespace ems {
namespace net {
namespace device {
namespace test {

class TestModbusServerHelper {
   public:
    /** @brief 构造 TestModbusServerHelper 实例。 */
    TestModbusServerHelper() = default;
    /** @brief 释放 TestModbusServerHelper 持有的资源。 */
    ~TestModbusServerHelper();

    /**
     * @brief 配置并启动测试用 Modbus 服务端。
     * @param[in] handler 待销毁的协议处理器。
     * @param[in] port 服务监听端口。
     */
    void setup_server(ModbusServerHandler& handler, int port);

    /**
     * @brief 停止并清理测试用 Modbus 服务端。
     * @param[in] handler 待销毁的协议处理器。
     */
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
