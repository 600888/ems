/**
 * @file tcp_client.cpp
 * @brief Modbus TCP 客户端示例
 *
 * 本示例展示如何使用 ModbusClient 连接 Modbus TCP 服务器，
 * 读取和写入各种类型的寄存器数据。
 *
 * 编译: g++ -std=c++20 -o tcp_client tcp_client.cpp \
 *       -I../src/net/protocol/modbus \
 *       -I../3rd/libmodbuspp/include \
 *       -L../build/3rd/libmodbuspp -lmodbuspp \
 *       -lpthread
 *
 * 运行: ./tcp_client [IP] [Port] [SlaveID]
 *       默认: ./tcp_client 127.0.0.1 502 1
 */

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

#include "modbus_client.h"

using namespace ems::net::modbus;

static std::atomic<bool> g_running{true};
static int g_slave_id = 1;

void signalHandler(int sig) { g_running = false; }

static float registersToFloat(const uint16_t* reg) {
    uint32_t bits = (static_cast<uint32_t>(reg[0]) << 16) | reg[1];
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

static double registersToDouble(const uint16_t* reg) {
    uint64_t bits = (static_cast<uint64_t>(reg[0]) << 48) | (static_cast<uint64_t>(reg[1]) << 32) |
                    (static_cast<uint64_t>(reg[2]) << 16) | reg[3];
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

static int32_t registersToInt32(const uint16_t* reg) {
    return (static_cast<int32_t>(reg[0]) << 16) | reg[1];
}

static uint32_t registersToUInt32(const uint16_t* reg) {
    return (static_cast<uint32_t>(reg[0]) << 16) | reg[1];
}

static int64_t registersToInt64(const uint16_t* reg) {
    return (static_cast<int64_t>(reg[0]) << 48) | (static_cast<int64_t>(reg[1]) << 32) |
           (static_cast<int64_t>(reg[2]) << 16) | reg[3];
}

void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           Modbus TCP Client Example (libmodbuspp)             ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  功能: 演示 Modbus TCP 客户端的各种寄存器读写操作             ║\n";
    std::cout << "║  支持: int16, uint16, int32, uint32, float, double, int64    ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

bool demoBasicRegisters(ModbusClient& client, int slave_id) {
    std::cout << "\n[1] 基本寄存器读写 (Read/Write Registers)\n";
    std::cout << std::string(60, '-') << "\n";

    uint16_t write_data[] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
    uint16_t read_data[4];

    if (client.writeMultipleRegisters(slave_id, 0, write_data, 4) ==
        ModbusClient::ErrorCode::Success) {
        std::cout << "写入保持寄存器 [0-3]: ";
        for (int i = 0; i < 4; i++) {
            std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << write_data[i]
                      << " ";
        }
        std::cout << std::dec << "\n";
    } else {
        std::cerr << "写入失败: " << client.getLastErrorMsg() << "\n";
        return false;
    }

    if (client.readHoldingRegisters(slave_id, 0, read_data, 4) ==
        ModbusClient::ErrorCode::Success) {
        std::cout << "读取保持寄存器 [0-3]: ";
        for (int i = 0; i < 4; i++) {
            std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << read_data[i]
                      << " ";
        }
        std::cout << std::dec << "\n";
    } else {
        std::cerr << "读取失败: " << client.getLastErrorMsg() << "\n";
        return false;
    }

    if (client.writeSingleRegister(slave_id, 10, 12345) == ModbusClient::ErrorCode::Success) {
        uint16_t value;
        client.readHoldingRegisters(slave_id, 10, &value, 1);
        std::cout << "写入单寄存器 [10] = " << value << "\n";
    }

    return true;
}

bool demoCoils(ModbusClient& client, int slave_id) {
    std::cout << "\n[2] 线圈读写 (Read/Write Coils)\n";
    std::cout << std::string(60, '-') << "\n";

    bool write_coils[] = {true, false, true, true, false, false, true, false};
    bool read_coils[8];

    if (client.writeMultipleCoils(slave_id, 0, write_coils, 8) ==
        ModbusClient::ErrorCode::Success) {
        std::cout << "写入线圈 [0-7]: ";
        for (int i = 0; i < 8; i++) {
            std::cout << (write_coils[i] ? "[1]" : "[0]");
        }
        std::cout << "\n";
    }

    if (client.readCoils(slave_id, 0, read_coils, 8) == ModbusClient::ErrorCode::Success) {
        std::cout << "读取线圈 [0-7]: ";
        for (int i = 0; i < 8; i++) {
            std::cout << (read_coils[i] ? "[1]" : "[0]");
        }
        std::cout << "\n";
    }

    client.writeSingleCoil(slave_id, 0, true);
    bool coil_state;
    client.readCoils(slave_id, 0, &coil_state, 1);
    std::cout << "单线圈写入/读取 [0] = " << (coil_state ? "ON" : "OFF") << "\n";

    return true;
}

bool demoInt32(ModbusClient& client, int slave_id) {
    std::cout << "\n[3] 32位整数读写 (int32_t / uint32_t)\n";
    std::cout << std::string(60, '-') << "\n";

    int32_t power_value = 1234567;
    Modbus::Data<int32_t, Modbus::EndianBig> power_data(power_value);

    std::cout << "int32_t [200-201] (EndianBig):\n";
    std::cout << "  寄存器值: ";
    for (size_t i = 0; i < power_data.registers().size(); i++) {
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                  << power_data.registers()[i] << " ";
    }
    std::cout << std::dec << "\n";

    if (client.writeMultipleRegisters(slave_id, 200, power_data.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 200, reg, 2);
        int32_t read_power = registersToInt32(reg);
        std::cout << "  写入值 = " << power_value << ", 读取值 = " << read_power << "\n";
    }

    uint32_t timestamp = 1700000000;
    Modbus::Data<uint32_t, Modbus::EndianBig> ts_data(timestamp);
    if (client.writeMultipleRegisters(slave_id, 202, ts_data.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 202, reg, 2);
        uint32_t read_ts = registersToUInt32(reg);
        std::cout << "uint32_t [202-203]: 写入值 = " << read_ts << "\n";
    }

    return true;
}

bool demoFloat(ModbusClient& client, int slave_id) {
    std::cout << "\n[4] 32位浮点数读写 (float)\n";
    std::cout << std::string(60, '-') << "\n";

    Modbus::Data<float, Modbus::EndianBig> temp_data(25.5f);
    std::cout << "float [300-301] (EndianBig):\n";
    std::cout << "  寄存器值: ";
    for (size_t i = 0; i < temp_data.registers().size(); i++) {
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                  << temp_data.registers()[i] << " ";
    }
    std::cout << std::dec << "\n";

    if (client.writeMultipleRegisters(slave_id, 300, temp_data.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 300, reg, 2);
        float read_temp = registersToFloat(reg);
        std::cout << "  写入值 = " << std::fixed << std::setprecision(2) << temp_data.value()
                  << ", 读取值 = " << read_temp << " °C\n";
    }

    Modbus::Data<float, Modbus::EndianBig> voltage(220.75f);
    if (client.writeMultipleRegisters(slave_id, 302, voltage.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 302, reg, 2);
        float read_v = registersToFloat(reg);
        std::cout << "  电压 [302-303]: " << read_v << " V\n";
    }

    return true;
}

bool demoDouble(ModbusClient& client, int slave_id) {
    std::cout << "\n[5] 64位浮点数读写 (double)\n";
    std::cout << std::string(60, '-') << "\n";

    Modbus::Data<double, Modbus::EndianBig> pi_data(3.141592653589793);
    std::cout << "double [400-403] (EndianBig, 占用4个寄存器):\n";
    std::cout << "  寄存器值: ";
    for (size_t i = 0; i < pi_data.registers().size(); i++) {
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << pi_data.registers()[i]
                  << " ";
    }
    std::cout << std::dec << "\n";

    if (client.writeMultipleRegisters(slave_id, 400, pi_data.registers().data(), 4) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[4];
        client.readHoldingRegisters(slave_id, 400, reg, 4);
        double read_pi = registersToDouble(reg);
        std::cout << "  写入值 = " << std::setprecision(15) << pi_data.value()
                  << ", 读取值 = " << read_pi << "\n";
    }

    return true;
}

bool demoInt64(ModbusClient& client, int slave_id) {
    std::cout << "\n[6] 64位整数读写 (int64_t / uint64_t)\n";
    std::cout << std::string(60, '-') << "\n";

    int64_t big_value = 12345678901234LL;
    Modbus::Data<int64_t, Modbus::EndianBig> big_data(big_value);

    std::cout << "int64_t [500-503] (EndianBig, 占用4个寄存器):\n";
    std::cout << "  寄存器值: ";
    for (size_t i = 0; i < big_data.registers().size(); i++) {
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                  << big_data.registers()[i] << " ";
    }
    std::cout << std::dec << "\n";

    if (client.writeMultipleRegisters(slave_id, 500, big_data.registers().data(), 4) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[4];
        client.readHoldingRegisters(slave_id, 500, reg, 4);
        int64_t read_big = registersToInt64(reg);
        std::cout << "  写入值 = " << big_value << ", 读取值 = " << read_big << "\n";
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    std::string ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? std::atoi(argv[2]) : 502;
    g_slave_id = (argc > 3) ? std::atoi(argv[3]) : 1;

    std::cout << "连接参数:\n";
    std::cout << "  IP: " << ip << "\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Slave ID: " << g_slave_id << "\n\n";

    ModbusClient client(ModbusClient::NetType::TCP, ip, std::to_string(port));
    client.setResponseTimeout(3000);
    client.setByteTimeout(1000);
    client.addSlave(1);

    std::cout << "正在连接 Modbus TCP 服务器...\n";
    if (!client.connect()) {
        std::cerr << "连接失败: " << client.getLastErrorMsg() << "\n";
        std::cerr << "\n提示: 如果没有服务器运行，可以先启动 tcp_server.cpp 示例\n";
        return 1;
    }

    std::cout << "连接成功!\n\n";

    int test_count = 0;
    while (g_running.load()) {
        std::cout << "\n[" << ++test_count << "] 执行测试轮次\n";
        std::cout << "按 Ctrl+C 退出\n";

        demoBasicRegisters(client, g_slave_id);
        demoCoils(client, g_slave_id);
        demoInt32(client, g_slave_id);
        demoFloat(client, g_slave_id);
        demoDouble(client, g_slave_id);
        demoInt64(client, g_slave_id);

        std::cout << "\n等待 5 秒后进行下一轮测试...\n";
        for (int i = 5; i > 0 && g_running.load(); i--) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "\r  " << i << "s ";
            std::cout.flush();
        }
        std::cout << "\n";
    }

    std::cout << "\n断开连接...\n";
    client.disconnect();

    std::cout << "客户端已退出.\n";
    return 0;
}
