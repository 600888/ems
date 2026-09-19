/**
 * @file rtu_client.cpp
 * @brief Modbus RTU 客户端示例
 *
 * 本示例展示如何使用 ModbusClient 连接 Modbus RTU 从设备，
 * 通过串口进行各种寄存器的读写操作。
 *
 * 编译: g++ -std=c++20 -o rtu_client rtu_client.cpp \
 *       -I../src/net/protocol/modbus \
 *       -I../3rd/libmodbuspp/include \
 *       -L../build/3rd/libmodbuspp -lmodbuspp \
 *       -lpthread
 *
 * 运行: ./rtu_client [device] [Baud] [SlaveID]
 *       默认: ./rtu_client /dev/ttyUSB0 9600 1
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

static int32_t registersToInt32(const uint16_t* reg) {
    return (static_cast<int32_t>(reg[0]) << 16) | reg[1];
}

void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           Modbus RTU Client Example (libmodbuspp)            ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  功能: 演示 Modbus RTU 客户端的各种寄存器读写操作             ║\n";
    std::cout << "║  通信: 串口 (RS232/RS485)                                    ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

std::string buildRtuSettings(int baud, char parity = 'N', int data_bits = 8, int stop_bits = 1) {
    return std::to_string(baud) + parity + std::to_string(stop_bits);
}

bool demoBasicOperations(ModbusClient& client, int slave_id) {
    std::cout << "\n[1] 基本寄存器操作\n";
    std::cout << std::string(60, '-') << "\n";

    uint16_t test_value = 0x1234;
    if (client.writeSingleRegister(slave_id, 0, test_value) == ModbusClient::ErrorCode::Success) {
        uint16_t read_value;
        if (client.readHoldingRegisters(slave_id, 0, &read_value, 1) ==
            ModbusClient::ErrorCode::Success) {
            std::cout << "单寄存器 R/W: 写入=0x" << std::hex << test_value << ", 读取=0x"
                      << read_value << std::dec;
            if (test_value == read_value) {
                std::cout << " [OK]\n";
            } else {
                std::cout << " [FAIL]\n";
            }
        }
    } else {
        std::cerr << "单寄存器操作失败\n";
        return false;
    }

    uint16_t multi_write[] = {100, 200, 300, 400, 500};
    if (client.writeMultipleRegisters(slave_id, 10, multi_write, 5) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t multi_read[5];
        if (client.readHoldingRegisters(slave_id, 10, multi_read, 5) ==
            ModbusClient::ErrorCode::Success) {
            std::cout << "多寄存器 R/W [10-14]: ";
            bool ok = true;
            for (int i = 0; i < 5; i++) {
                std::cout << multi_read[i];
                if (multi_read[i] != multi_write[i]) ok = false;
                if (i < 4) std::cout << ", ";
            }
            std::cout << (ok ? " [OK]\n" : " [FAIL]\n");
        }
    }

    return true;
}

bool demoCoilOperations(ModbusClient& client, int slave_id) {
    std::cout << "\n[2] 线圈操作\n";
    std::cout << std::string(60, '-') << "\n";

    bool coil_write[] = {true, false, true, false, true, false, true, false};
    if (client.writeMultipleCoils(slave_id, 0, coil_write, 8) == ModbusClient::ErrorCode::Success) {
        bool coil_read[8];
        if (client.readCoils(slave_id, 0, coil_read, 8) == ModbusClient::ErrorCode::Success) {
            std::cout << "线圈 [0-7]: ";
            bool ok = true;
            for (int i = 0; i < 8; i++) {
                std::cout << (coil_read[i] ? "1" : "0");
                if (coil_read[i] != coil_write[i]) ok = false;
            }
            std::cout << (ok ? " [OK]\n" : " [FAIL]\n");
        }
    }

    client.writeSingleCoil(slave_id, 16, true);
    bool state;
    client.readCoils(slave_id, 16, &state, 1);
    std::cout << "单线圈 [16]: " << (state ? "ON" : "OFF") << "\n";

    return true;
}

bool demoAnalogSensor(ModbusClient& client, int slave_id) {
    std::cout << "\n[3] 模拟量传感器数据读取\n";
    std::cout << std::string(60, '-') << "\n";

    uint16_t raw_adc[8];
    if (client.readInputRegisters(slave_id, 0, raw_adc, 8) == ModbusClient::ErrorCode::Success) {
        std::cout << "传感器原始值 [0-7]: ";
        for (int i = 0; i < 8; i++) {
            std::cout << raw_adc[i];
            if (i < 7) std::cout << ", ";
        }
        std::cout << "\n";

        constexpr float kFactor = 0.01f;
        std::cout << "转换后的物理值:\n";
        std::cout << "  温度1: " << std::fixed << std::setprecision(2) << raw_adc[0] * kFactor
                  << " °C\n";
        std::cout << "  温度2: " << raw_adc[1] * kFactor << " °C\n";
        std::cout << "  湿度:   " << raw_adc[2] * kFactor << " %\n";
        std::cout << "  压力:   " << raw_adc[3] * kFactor << " bar\n";
    } else {
        std::cout << "读取传感器数据失败 (设备可能不支持)\n";
    }

    return true;
}

bool demoFloatData(ModbusClient& client, int slave_id) {
    std::cout << "\n[4] 浮点数数据读写\n";
    std::cout << std::string(60, '-') << "\n";

    Modbus::Data<float, Modbus::EndianBig> temp_write(25.5f);
    if (client.writeMultipleRegisters(slave_id, 100, temp_write.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 100, reg, 2);
        float temp_read = registersToFloat(reg);
        std::cout << "温度 [100-101]: 写入=" << temp_write.value() << ", 读取=" << temp_read
                  << " °C\n";
    }

    Modbus::Data<float, Modbus::EndianBig> voltage_write(220.75f);
    if (client.writeMultipleRegisters(slave_id, 102, voltage_write.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 102, reg, 2);
        float voltage_read = registersToFloat(reg);
        std::cout << "电压 [102-103]: 写入=" << voltage_write.value() << ", 读取=" << voltage_read
                  << " V\n";
    }

    return true;
}

bool demoInt32Data(ModbusClient& client, int slave_id) {
    std::cout << "\n[5] 32位整数数据读写\n";
    std::cout << std::string(60, '-') << "\n";

    int32_t counter_value = 12345678;
    Modbus::Data<int32_t, Modbus::EndianBig> counter_write(counter_value);

    std::cout << "计数器 [200-201]:\n";
    std::cout << "  寄存器: ";
    for (size_t i = 0; i < counter_write.registers().size(); i++) {
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0')
                  << counter_write.registers()[i] << " ";
    }
    std::cout << std::dec << "\n";

    if (client.writeMultipleRegisters(slave_id, 200, counter_write.registers().data(), 2) ==
        ModbusClient::ErrorCode::Success) {
        uint16_t reg[2];
        client.readHoldingRegisters(slave_id, 200, reg, 2);
        int32_t counter_read = registersToInt32(reg);
        std::cout << "  写入值=" << counter_value << ", 读取值=" << counter_read << "\n";
    }

    return true;
}

bool demoConfigRegister(ModbusClient& client, int slave_id) {
    std::cout << "\n[6] 配置寄存器写入\n";
    std::cout << std::string(60, '-') << "\n";

    struct ConfigRegister {
        uint16_t addr;
        const char* name;
        uint16_t value;
    } configs[] = {{300, "采样率", 100},
                   {301, "滤波系数", 50},
                   {302, "报警阈值高", 1000},
                   {303, "报警阈值低", 100},
                   {304, "通信超时", 3000}};

    std::cout << "写入配置寄存器:\n";
    for (const auto& cfg : configs) {
        if (client.writeSingleRegister(slave_id, cfg.addr, cfg.value) ==
            ModbusClient::ErrorCode::Success) {
            uint16_t read_val;
            client.readHoldingRegisters(slave_id, cfg.addr, &read_val, 1);
            std::cout << "  [" << cfg.addr << "] " << cfg.name << " = " << read_val << "\n";
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    std::string device = (argc > 1) ? argv[1] : "/dev/ttyUSB0";
    int baud = (argc > 2) ? std::atoi(argv[2]) : 9600;
    g_slave_id = (argc > 3) ? std::atoi(argv[3]) : 1;

    std::string settings = buildRtuSettings(baud, 'N', 8, 1);

    std::cout << "连接参数:\n";
    std::cout << "  串口设备: " << device << "\n";
    std::cout << "  波特率: " << baud << "\n";
    std::cout << "  校验: N (无校验)\n";
    std::cout << "  数据位: 8\n";
    std::cout << "  停止位: 1\n";
    std::cout << "  从机地址: " << g_slave_id << "\n";
    std::cout << "  设置字符串: " << settings << "\n\n";

    ModbusClient client(ModbusClient::NetType::RTU, device, settings);
    client.setResponseTimeout(3000);
    client.setByteTimeout(1000);

    std::cout << "正在连接 Modbus RTU 从设备...\n";
    if (!client.connect()) {
        std::cerr << "连接失败: " << client.getLastErrorMsg() << "\n";
        std::cerr << "\n提示: 请检查:\n";
        std::cerr << "  1. 串口设备是否存在且可访问\n";
        std::cerr << "  2. 串口参数是否正确\n";
        std::cerr << "  3. 从设备是否上电\n";
        std::cerr << "  4. RS485转换器是否正常工作\n";
        return 1;
    }

    std::cout << "连接成功!\n\n";

    int poll_count = 0;
    while (g_running.load()) {
        std::cout << "\n[" << ++poll_count << "] 数据轮询\n";
        std::cout << "按 Ctrl+C 退出\n";

        demoBasicOperations(client, g_slave_id);
        demoCoilOperations(client, g_slave_id);
        demoAnalogSensor(client, g_slave_id);
        demoFloatData(client, g_slave_id);
        demoInt32Data(client, g_slave_id);
        demoConfigRegister(client, g_slave_id);

        std::cout << "\n等待 5 秒后进行下一轮...\n";
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
