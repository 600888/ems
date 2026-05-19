#include <gtest/gtest.h>
#include <memory>
#include <unordered_map>
#include <future>
#include <chrono>
#include <thread>
#include "test_utils.h"

namespace ems {
namespace net {
namespace device {
namespace test {

TEST(ModbusServerHandler, Constructor) {
    ModbusServerHandler<TestPoint> handler;
    EXPECT_NE(handler.server(), nullptr);
}

TEST(ModbusServerHandler, ConstructorWithLogger) {
    bool logger_called = false;
    auto logger = [&logger_called](const std::string& msg) {
        logger_called = true;
        EXPECT_TRUE(msg.find("Modbus") != std::string::npos);
    };
    
    ModbusServerHandler<TestPoint> handler(logger);
    EXPECT_NE(handler.server(), nullptr);
}

TEST(ModbusServerHandler, InitializeWithTCPConfig) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5020;
    config["protocol_type"] = 1;
    
    handler.initialize(config);
    EXPECT_NE(handler.server(), nullptr);
}

TEST(ModbusServerHandler, InitializeWithRTUConfig) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["protocol_type"] = 0;
    config["serial_port"] = std::string("/dev/ttyUSB0");
    config["baudrate"] = 9600;
    config["parity"] = std::string("N");
    config["stopbits"] = 1;
    
    handler.initialize(config);
    EXPECT_NE(handler.server(), nullptr);
}

TEST(ModbusServerHandler, StartAndStop) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5021;
    handler.initialize(config);
    
    auto start_future = handler.start();
    bool started = start_future.get();
    EXPECT_TRUE(started);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto stop_future = handler.stop();
    bool stopped = stop_future.get();
    EXPECT_TRUE(stopped);
}

TEST(ModbusServerHandler, GetSetValueByAddressHoldingRegister) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5022;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.setValueByAddress(3, 1, 0, 12345);
    auto value = handler.getValueByAddress(3, 1, 0);
    
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 12345);
    
    handler.stop();
}

TEST(ModbusServerHandler, GetSetValueByAddressInputRegister) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5023;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto server = handler.getServer();
    ASSERT_NE(server, nullptr);
    
    server->writeInputRegister(1, 10, 54321);
    auto value = handler.getValueByAddress(4, 1, 10);
    
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 54321);
    
    handler.stop();
}

TEST(ModbusServerHandler, GetSetValueByAddressCoil) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5024;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.setValueByAddress(5, 1, 0, true);
    auto value = handler.getValueByAddress(1, 1, 0);
    
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 1);
    
    handler.stop();
}

TEST(ModbusServerHandler, GetSetValueWithDecodeInt16) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5025;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.setValueByAddressWithDecode(3, 1, 0, 100, "int16");
    auto value = handler.getValueByAddressWithDecode(3, 1, 0, "int16");
    
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 100);
    
    handler.stop();
}

TEST(ModbusServerHandler, GetSetValueWithDecodeUInt32) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5026;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int test_value = 1234567890;
    handler.setValueByAddressWithDecode(3, 1, 0, test_value, "uint32");
    auto value = handler.getValueByAddressWithDecode(3, 1, 0, "uint32");
    
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, test_value);
    
    handler.stop();
}

TEST(ModbusServerHandler, AddPoints) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5027;
    handler.initialize(config);
    
    auto point1 = std::make_shared<TestPoint>();
    point1->rtu_addr = 1;
    point1->address = 0;
    point1->func_code = 3;
    
    auto point2 = std::make_shared<TestPoint>();
    point2->rtu_addr = 1;
    point2->address = 1;
    point2->func_code = 3;
    
    std::vector<std::shared_ptr<TestPoint>> points = {point1, point2};
    handler.addPoints(points);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.setValueByAddress(3, 1, 0, 100);
    handler.setValueByAddress(3, 1, 1, 200);
    
    auto value1 = handler.readValue(point1);
    auto value2 = handler.readValue(point2);
    
    EXPECT_EQ(std::get<int>(value1), 100);
    EXPECT_EQ(std::get<int>(value2), 200);
    
    handler.stop();
}

TEST(ModbusServerHandler, WriteValueToPoint) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5028;
    handler.initialize(config);
    
    auto point = std::make_shared<TestPoint>();
    point->rtu_addr = 1;
    point->address = 0;
    point->func_code = 3;
    
    handler.addPoints({point});
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    bool result = handler.writeValue(point, 999);
    EXPECT_TRUE(result);
    
    auto value = handler.readValue(point);
    EXPECT_EQ(std::get<int>(value), 999);
    
    handler.stop();
}

TEST(ModbusServerHandler, CapturedMessages) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5029;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.setValueByAddress(3, 1, 0, 123);
    handler.setValueByAddress(3, 1, 1, 456);
    
    auto messages = handler.getCapturedMessages(10);
    EXPECT_GE(messages.size(), static_cast<size_t>(0));
    
    handler.clearCapturedMessages();
    messages = handler.getCapturedMessages(10);
    EXPECT_EQ(messages.size(), static_cast<size_t>(0));
    
    handler.stop();
}

TEST(ModbusServerHandler, AvgTimeStats) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5030;
    handler.initialize(config);
    
    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto stats = handler.getAvgTime();
    EXPECT_GE(stats.tx_count, 0);
    EXPECT_GE(stats.rx_count, 0);
    
    handler.stop();
}

TEST(ModbusServerHandler, WriteCallback) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5031;
    handler.initialize(config);
    
    bool callbackCalled = false;
    int receivedSlaveId = 0;
    int receivedFuncCode = 0;
    int receivedAddress = 0;
    
    struct MockCallback : public ModbusWriteCallback {
        bool& called;
        int& slaveId;
        int& funcCode;
        int& address;
        
        MockCallback(bool& c, int& s, int& f, int& a)
            : called(c), slaveId(s), funcCode(f), address(a) {}
        
        void onModbusClientWrite(int s, int f, int a,
                                     const std::vector<int>& values) override {
            called = true;
            slaveId = s;
            funcCode = f;
            address = a;
        }
    };
    
    auto callback = std::make_shared<MockCallback>(callbackCalled, receivedSlaveId,
                                                     receivedFuncCode, receivedAddress);
    handler.setWriteCallback(callback);
    
    auto startFuture = handler.start();
    startFuture.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    handler.stop();
}

TEST(ModbusClientHandler, ConstructorWithLogger) {
    bool logger_called = false;
    auto logger = [&logger_called](const std::string& msg) {
        logger_called = true;
    };
    
    ModbusClientHandler<TestPoint> handler(logger);
    EXPECT_NE(handler.client(), nullptr);
}

TEST(ModbusClientHandler, InitializeWithTCPConfig) {
    ModbusClientHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["ip"] = std::string("127.0.0.1");
    config["port"] = 5020;
    config["protocol_type"] = 1;
    config["timeout"] = 5000;
    
    handler.initialize(config);
    EXPECT_NE(handler.client(), nullptr);
}

TEST(ModbusClientHandler, InitializeWithRTUConfig) {
    ModbusClientHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["protocol_type"] = 0;
    config["serial_port"] = std::string("/dev/ttyUSB0");
    config["baudrate"] = 9600;
    config["parity"] = std::string("N");
    config["stopbits"] = 1;
    
    handler.initialize(config);
    EXPECT_NE(handler.client(), nullptr);
}

TEST(ModbusClientHandler, ConnectAndDisconnect) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5032;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5032;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    EXPECT_TRUE(connected);
    
    bool is_connected = client_handler.isConnected();
    EXPECT_TRUE(is_connected);
    
    client_handler.disconnect();
    bool is_disconnected = !client_handler.isConnected();
    EXPECT_TRUE(is_disconnected);
    
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadHoldingRegisters) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5033;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server_handler.setValueByAddress(3, 1, 0, 12345);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5033;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto registers = client_handler.readHoldingRegisters(1, 0, 1);
    ASSERT_EQ(registers.size(), 1);
    EXPECT_EQ(registers[0], 12345);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadInputRegisters) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5034;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto server = server_handler.getServer();
    server->writeInputRegister(1, 0, 54321);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5034;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto registers = client_handler.readInputRegisters(1, 0, 1);
    ASSERT_EQ(registers.size(), 1);
    EXPECT_EQ(registers[0], 54321);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadCoils) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5035;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server_handler.setValueByAddress(5, 1, 0, true);
    server_handler.setValueByAddress(5, 1, 1, false);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5035;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto coils = client_handler.readCoils(1, 0, 2);
    ASSERT_EQ(coils.size(), 2);
    EXPECT_TRUE(coils[0]);
    EXPECT_FALSE(coils[1]);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteSingleRegister) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5036;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5036;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    bool write_result = client_handler.writeSingleRegister(1, 0, 99999);
    EXPECT_TRUE(write_result);
    
    auto value = server_handler.getValueByAddress(3, 1, 0);
    EXPECT_EQ(std::get<int>(value), 99999);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteMultipleRegisters) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5037;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5037;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    std::vector<int> values = {100, 200, 300};
    bool write_result = client_handler.writeMultipleRegisters(1, 0, values);
    EXPECT_TRUE(write_result);
    
    for (int i = 0; i < 3; ++i) {
        auto value = server_handler.getValueByAddress(3, 1, i);
        EXPECT_EQ(std::get<int>(value), values[i]);
    }
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteSingleCoil) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5038;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5038;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    bool write_result = client_handler.writeSingleCoil(1, 0, true);
    EXPECT_TRUE(write_result);
    
    auto value = server_handler.getValueByAddress(1, 1, 0);
    EXPECT_EQ(std::get<int>(value), 1);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteMultipleCoils) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5039;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5039;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    CoilBits coils(4);
    coils.set(0, true);
    coils.set(1, false);
    coils.set(2, true);
    coils.set(3, true);
    bool write_result = client_handler.writeMultipleCoils(1, 0, coils);
    EXPECT_TRUE(write_result);
    
    for (int i = 0; i < 4; ++i) {
        auto value = server_handler.getValueByAddress(1, 1, i);
        EXPECT_EQ(std::get<int>(value), coils[i] ? 1 : 0);
    }
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadValueFromPoint) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5040;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server_handler.setValueByAddress(3, 1, 0, 77777);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5040;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto point = std::make_shared<TestPoint>();
    point->rtu_addr = 1;
    point->address = 0;
    point->func_code = 3;
    
    auto value = client_handler.readValue(point);
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 77777);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteValueToPoint) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5041;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5041;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto point = std::make_shared<TestPoint>();
    point->rtu_addr = 1;
    point->address = 0;
    point->func_code = 6;
    
    bool write_result = client_handler.writeValue(point, 88888);
    EXPECT_TRUE(write_result);
    
    auto value = server_handler.getValueByAddress(3, 1, 0);
    EXPECT_EQ(std::get<int>(value), 88888);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadValueAsync) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5042;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server_handler.setValueByAddress(3, 1, 0, 55555);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5042;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto point = std::make_shared<TestPoint>();
    point->rtu_addr = 1;
    point->address = 0;
    point->func_code = 3;
    
    auto future = client_handler.readValueAsync(point);
    auto value = future.get();
    int int_value = std::get<int>(value);
    EXPECT_EQ(int_value, 55555);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, WriteValueAsync) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5043;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5043;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto point = std::make_shared<TestPoint>();
    point->rtu_addr = 1;
    point->address = 0;
    point->func_code = 6;
    
    auto future = client_handler.writeValueAsync(point, 66666);
    bool write_result = future.get();
    EXPECT_TRUE(write_result);
    
    auto value = server_handler.getValueByAddress(3, 1, 0);
    EXPECT_EQ(std::get<int>(value), 66666);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusClientHandler, ReadRegistersBatch) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;
    
    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5044;
    server_handler.initialize(server_config);
    
    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    server_handler.setValueByAddress(3, 1, 0, 111);
    server_handler.setValueByAddress(3, 1, 1, 222);
    server_handler.setValueByAddress(3, 1, 2, 333);
    
    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5044;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);
    
    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);
    
    auto registers = client_handler.readRegistersBatchAsync(3, 1, 0, 3);
    ASSERT_EQ(registers.size(), 3);
    EXPECT_EQ(registers[0], 111);
    EXPECT_EQ(registers[1], 222);
    EXPECT_EQ(registers[2], 333);
    
    client_handler.disconnect();
    server_handler.stop();
}

TEST(ValueTypeConversion, IntConversion) {
    ValueType int_val = 42;
    ValueType double_val = 3.14;
    ValueType bool_val = true;
    ValueType string_val = std::string("test");
    
    EXPECT_TRUE(std::holds_alternative<int>(int_val));
    EXPECT_TRUE(std::holds_alternative<double>(double_val));
    EXPECT_TRUE(std::holds_alternative<bool>(bool_val));
    EXPECT_TRUE(std::holds_alternative<std::string>(string_val));
    
    EXPECT_EQ(std::get<int>(int_val), 42);
    EXPECT_EQ(std::get<bool>(bool_val), true);
}

TEST(ProtocolHandler, MessageCapture) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5045;
    handler.initialize(config);
    
    auto capture = handler.getMessageCapture();
    EXPECT_NE(capture, nullptr);
    
    std::vector<uint8_t> test_data = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    handler.addTxMessage(test_data, "Test TX");
    handler.addRxMessage(test_data, "Test RX");
    
    auto messages = handler.getCapturedMessages(10);
    EXPECT_GE(messages.size(), 2);
    
    handler.clearCapturedMessages();
    messages = handler.getCapturedMessages(10);
    EXPECT_EQ(messages.size(), 0);
    
    handler.stop();
}

TEST(ProtocolHandler, RunningState) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5046;
    handler.initialize(config);

    EXPECT_FALSE(handler.isRunning());

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(handler.isRunning());

    auto stop_future = handler.stop();
    stop_future.get();

    EXPECT_FALSE(handler.isRunning());
}

TEST(ModbusServerHandler, MultipleSlaves) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5047;
    handler.initialize(config);

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    handler.setValueByAddress(3, 1, 0, 100);
    handler.setValueByAddress(3, 2, 0, 200);
    handler.setValueByAddress(3, 3, 0, 300);

    EXPECT_EQ(std::get<int>(handler.getValueByAddress(3, 1, 0)), 100);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(3, 2, 0)), 200);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(3, 3, 0)), 300);

    handler.stop();
}

TEST(ModbusServerHandler, ReadWriteMultipleAddresses) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5048;
    handler.initialize(config);

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int base_addr = 100;
    const int count = 10;

    for (int i = 0; i < count; ++i) {
        handler.setValueByAddress(3, 1, base_addr + i, (i + 1) * 100);
    }

    for (int i = 0; i < count; ++i) {
        auto value = handler.getValueByAddress(3, 1, base_addr + i);
        EXPECT_EQ(std::get<int>(value), (i + 1) * 100);
    }

    handler.stop();
}

TEST(ModbusServerHandler, DifferentFunctionCodes) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5049;
    handler.initialize(config);

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    handler.setValueByAddress(3, 1, 0, 123);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(3, 1, 0)), 123);

    handler.setValueByAddress(4, 1, 0, 456);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(4, 1, 0)), 456);

    handler.setValueByAddress(5, 1, 0, true);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(1, 1, 0)), 1);

    handler.setValueByAddress(5, 1, 0, false);
    EXPECT_EQ(std::get<int>(handler.getValueByAddress(1, 1, 0)), 0);

    handler.stop();
}

TEST(ModbusClientHandler, DifferentSlaves) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;

    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5050;
    server_handler.initialize(server_config);

    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server_handler.setValueByAddress(3, 1, 0, 111);
    server_handler.setValueByAddress(3, 2, 0, 222);

    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5050;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);

    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);

    auto regs1 = client_handler.readHoldingRegisters(1, 0, 1);
    ASSERT_EQ(regs1.size(), 1);
    EXPECT_EQ(regs1[0], 111);

    auto regs2 = client_handler.readHoldingRegisters(2, 0, 1);
    ASSERT_EQ(regs2.size(), 1);
    EXPECT_EQ(regs2[0], 222);

    client_handler.disconnect();
    server_handler.stop();
}

TEST(ModbusServerHandler, EdgeCaseZeroValue) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5052;
    handler.initialize(config);

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    handler.setValueByAddress(3, 1, 0, 0);
    auto value = handler.getValueByAddress(3, 1, 0);
    EXPECT_EQ(std::get<int>(value), 0);

    handler.stop();
}

TEST(ModbusServerHandler, EdgeCaseMaxValue) {
    ModbusServerHandler<TestPoint> handler;
    std::unordered_map<std::string, ValueType> config;
    config["port"] = 5053;
    handler.initialize(config);

    auto start_future = handler.start();
    start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int max_value = 65535;
    handler.setValueByAddress(3, 1, 0, max_value);
    auto value = handler.getValueByAddress(3, 1, 0);
    EXPECT_EQ(std::get<int>(value), max_value);

    handler.stop();
}

TEST(ModbusClientHandler, ReadMultipleRegisters) {
    ModbusClientHandler<TestPoint> client_handler;
    ModbusServerHandler<TestPoint> server_handler;

    std::unordered_map<std::string, ValueType> server_config;
    server_config["port"] = 5054;
    server_handler.initialize(server_config);

    auto server_start_future = server_handler.start();
    server_start_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (int i = 0; i < 5; ++i) {
        server_handler.setValueByAddress(3, 1, i, (i + 1) * 100);
    }

    std::unordered_map<std::string, ValueType> client_config;
    client_config["ip"] = std::string("127.0.0.1");
    client_config["port"] = 5054;
    client_config["protocol_type"] = 1;
    client_handler.initialize(client_config);

    bool connected = client_handler.connect();
    ASSERT_TRUE(connected);

    auto registers = client_handler.readHoldingRegisters(1, 0, 5);
    ASSERT_EQ(registers.size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(registers[i], (i + 1) * 100);
    }

    client_handler.disconnect();
    server_handler.stop();
}

}
}
}
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}