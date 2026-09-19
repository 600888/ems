/**
 * @file rtu_server.cpp
 * @brief Modbus RTU 服务端示例
 *
 * 本示例展示如何使用 ModbusServer 创建 Modbus RTU 服务器，
 * 通过串口模拟从设备响应主站的请求。
 *
 * 编译: g++ -std=c++20 -o rtu_server rtu_server.cpp \
 *       -I../src/net/protocol/modbus \
 *       -I../3rd/libmodbuspp/include \
 *       -L../build/3rd/libmodbuspp -lmodbuspp \
 *       -lpthread
 *
 * 运行: ./rtu_server [device] [Baud]
 *       默认: ./rtu_server /dev/ttyUSB1 9600
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>

#include "modbus_server.h"

using namespace ems::net::modbus;

static std::atomic<bool> g_running{true};

void signalHandler(int sig) { g_running = false; }

void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           Modbus RTU Server Example (libmodbuspp)           ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  功能: 模拟 Modbus RTU 从设备                           ║\n";
    std::cout << "║  通信: 串口 (可被主站轮询)                              ║\n";
    std::cout << "║  从机: 地址1 (可配置)                                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

std::string buildRtuSettings(int baud, char parity = 'N', int data_bits = 8, int stop_bits = 1) {
    return std::to_string(baud) + parity + std::to_string(stop_bits);
}

class RTUSimulator {
   public:
    static float generateValue(float min, float max, float base = 0) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<float> d{0, 1};

        float value = base + d(gen) * ((max - min) * 0.1f);
        return std::max(min, std::min(max, value));
    }

    static uint16_t generateADC(float percentage) {
        static std::mt19937 gen{std::random_device{}()};
        static std::uniform_int_distribution<> d{0, 655};

        return static_cast<uint16_t>(d(gen) * percentage / 100.0f);
    }
};

void updateSensorData(ModbusServer& server, int slave_id) {
    uint16_t raw_temp = RTUSimulator::generateADC(50);
    uint16_t raw_humidity = RTUSimulator::generateADC(60);
    uint16_t raw_pressure = RTUSimulator::generateADC(101);

    server.writeInputRegister(slave_id, 0, raw_temp);
    server.writeInputRegister(slave_id, 1, raw_humidity);
    server.writeInputRegister(slave_id, 2, raw_pressure);

    float temperature = raw_temp * 0.01f;
    float humidity = raw_humidity * 0.01f;

    Modbus::Data<float, Modbus::EndianBig> temp_data(temperature);
    Modbus::Data<float, Modbus::EndianBig> hum_data(humidity);

    server.writeHoldingRegisters(slave_id, 0, temp_data.registers().data(), 2);
    server.writeHoldingRegisters(slave_id, 2, hum_data.registers().data(), 2);

    std::cout << "\r[RTU从机" << slave_id << "] 温度=" << std::fixed << std::setprecision(2)
              << temperature << "°C 湿度=" << humidity << "%     " << std::flush;
}

void updatePowerData(ModbusServer& server, int slave_id) {
    static uint32_t pulse_count = 0;
    pulse_count += std::rand() % 10 + 1;

    Modbus::Data<uint32_t, Modbus::EndianBig> pulse_data(pulse_count);
    server.writeInputRegisters(slave_id, 10, pulse_data.registers().data(), 2);

    uint16_t voltage = static_cast<uint16_t>(RTUSimulator::generateValue(2100, 2300, 2200));
    uint16_t current = static_cast<uint16_t>(RTUSimulator::generateValue(0, 1000, 500));

    server.writeInputRegister(slave_id, 12, voltage);
    server.writeInputRegister(slave_id, 13, current);
}

void updateIOStatus(ModbusServer& server, int slave_id) {
    bool di[16];
    for (int i = 0; i < 16; i++) {
        di[i] = (std::rand() % 2) == 1;
    }
    server.writeInputRegisters(slave_id, 0, reinterpret_cast<uint16_t*>(di), 16);

    std::cout << "\r[RTU从机" << slave_id << "] DI[0-15]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << (di[i] ? "1" : "0");
    }
    std::cout << " DO: ";
    for (int i = 0; i < 8; i++) {
        bool coil;
        server.readCoil(slave_id, i, coil);
        std::cout << (coil ? "1" : "0");
    }
    std::cout << "     " << std::flush;
}

void setupRTUServer(ModbusServer& server, int slave_id) {
    std::cout << "配置 RTU 从机...\n";

    server.addSlave(slave_id);

    server.setHoldingRegisterBlock(slave_id, 0, 20);
    server.setInputRegisterBlock(slave_id, 0, 20);
    server.setCoilBlock(slave_id, 0, 32);
    server.setDiscreteInputBlock(slave_id, 0, 16);

    std::cout << "  从机地址: " << slave_id << "\n";
    std::cout << "  保持寄存器: [0-19]\n";
    std::cout << "  输入寄存器: [0-19]\n";
    std::cout << "  线圈: [0-31]\n";
    std::cout << "  离散输入: [0-15]\n";

    server.setDataChangeCallback([slave_id](int id, Modbus::Table table, int addr, int nb) {
        if (id == slave_id) {
            const char* table_name = (table == Modbus::Table::HoldingRegister) ? "HR"
                                     : (table == Modbus::Table::InputRegister) ? "IR"
                                     : (table == Modbus::Table::Coil)          ? "CO"
                                                                               : "DI";
            std::cout << "\n[变更通知] " << table_name << " [" << addr << "-" << addr + nb - 1
                      << "]\n";
        }
    });

    Modbus::Data<float, Modbus::EndianBig> version(1.0f);
    server.writeHoldingRegisters(slave_id, 100, version.registers().data(), 2);

    uint16_t model_info[] = {0x4D, 0x42, 0x54, 0x55};
    server.writeHoldingRegisters(slave_id, 102, model_info, 4);

    std::cout << "从机配置完成!\n\n";
}

static float registersToFloat(const uint16_t* reg) {
    uint32_t bits = (static_cast<uint32_t>(reg[0]) << 16) | reg[1];
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

static int32_t registersToInt32(const uint16_t* reg) {
    return (static_cast<int32_t>(reg[0]) << 16) | reg[1];
}

bool demoWriteOperations(ModbusServer& server, int slave_id) {
    std::cout << "\n\n========== 写入测试 ==========\n";

    std::cout << "\n[1] 写入单寄存器\n";
    server.writeHoldingRegister(slave_id, 0, 12345);
    uint16_t val;
    server.readHoldingRegister(slave_id, 0, val);
    std::cout << "  写入: 12345 -> 读取: " << val << "\n";

    std::cout << "\n[2] 写入浮点数\n";
    Modbus::Data<float, Modbus::EndianBig> fval(25.5f);
    server.writeHoldingRegisters(slave_id, 10, fval.registers().data(), 2);
    uint16_t reg_float[2];
    server.readHoldingRegisters(slave_id, 10, reg_float, 2);
    float fread = registersToFloat(reg_float);
    std::cout << "  写入: " << fval.value() << " -> 读取: " << fread << "\n";

    std::cout << "\n[3] 写入32位整数\n";
    Modbus::Data<int32_t, Modbus::EndianBig> ival(12345678);
    server.writeHoldingRegisters(slave_id, 12, ival.registers().data(), 2);
    uint16_t reg_int[2];
    server.readHoldingRegisters(slave_id, 12, reg_int, 2);
    int32_t iread = registersToInt32(reg_int);
    std::cout << "  写入: " << ival.value() << " -> 读取: " << iread << "\n";

    std::cout << "\n[4] 写入线圈\n";
    bool coils[] = {true, false, true, true, false, true, false, false};
    server.writeCoils(slave_id, 0, coils, 8);
    bool rcoils[8];
    server.readCoils(slave_id, 0, rcoils, 8);
    std::cout << "  写入: ";
    for (int i = 0; i < 8; i++) std::cout << (coils[i] ? "1" : "0");
    std::cout << " -> 读取: ";
    for (int i = 0; i < 8; i++) std::cout << (rcoils[i] ? "1" : "0");
    std::cout << "\n";

    std::cout << "\n========== 写入测试完成 ==========\n";
    return true;
}

bool demoReadOperations(ModbusServer& server, int slave_id) {
    std::cout << "\n========== 读取测试 ==========\n";

    std::cout << "\n[1] 读取保持寄存器\n";
    uint16_t hr[10];
    server.readHoldingRegisters(slave_id, 0, hr, 10);
    std::cout << "  HR[0-9]: ";
    for (int i = 0; i < 10; i++) {
        std::cout << hr[i];
        if (i < 9) std::cout << ", ";
    }
    std::cout << "\n";

    std::cout << "\n[2] 读取输入寄存器\n";
    uint16_t ir[10];
    server.readInputRegisters(slave_id, 0, ir, 10);
    std::cout << "  IR[0-9]: ";
    for (int i = 0; i < 10; i++) {
        std::cout << ir[i];
        if (i < 9) std::cout << ", ";
    }
    std::cout << "\n";

    std::cout << "\n[3] 读取线圈\n";
    bool co[16];
    server.readCoils(slave_id, 0, co, 16);
    std::cout << "  CO[0-15]: ";
    for (int i = 0; i < 16; i++) {
        std::cout << (co[i] ? "1" : "0");
    }
    std::cout << "\n";

    std::cout << "\n[4] 读取离散输入\n";
    bool di[16];
    server.readDiscreteInputs(slave_id, 0, di, 16);
    std::cout << "  DI[0-15]: ";
    for (int i = 0; i < 16; i++) {
        std::cout << (di[i] ? "1" : "0");
    }
    std::cout << "\n";

    std::cout << "\n========== 读取测试完成 ==========\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    std::string device = (argc > 1) ? argv[1] : "/dev/ttyUSB1";
    int baud = (argc > 2) ? std::atoi(argv[2]) : 9600;
    int slave_id = 1;

    std::string settings = buildRtuSettings(baud, 'N', 8, 1);

    std::cout << "服务参数:\n";
    std::cout << "  串口设备: " << device << "\n";
    std::cout << "  波特率: " << baud << "\n";
    std::cout << "  设置: " << settings << "\n";
    std::cout << "  从机地址: " << slave_id << "\n\n";

    ModbusServer server(ModbusServer::NetType::RTU, device, settings);

    setupRTUServer(server, slave_id);

    std::cout << "正在启动 Modbus RTU 服务器...\n";
    if (!server.start()) {
        std::cerr << "启动失败!\n";
        std::cerr << "\n提示: 请检查:\n";
        std::cerr << "  1. 串口设备是否存在\n";
        std::cerr << "  2. 串口是否被其他程序占用\n";
        std::cerr << "  3. 串口参数是否正确\n";
        return 1;
    }

    std::cout << "服务器已启动!\n";
    std::cout << "等待 Modbus 主站轮询...\n\n";

    std::cout << "\n开始数据模拟 (按 Ctrl+C 停止)...\n";
    std::cout << std::string(60, '-') << "\n";

    auto last_manual_demo = std::chrono::steady_clock::now();

    while (g_running.load()) {
        updateSensorData(server, slave_id);
        updatePowerData(server, slave_id);
        updateIOStatus(server, slave_id);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_manual_demo).count() >=
            30) {
            last_manual_demo = now;
            demoWriteOperations(server, slave_id);
            demoReadOperations(server, slave_id);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n\n正在停止服务器...\n";
    server.stop();

    std::cout << "服务器已停止.\n";
    return 0;
}
