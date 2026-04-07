/**
 * @file tcp_server.cpp
 * @brief Modbus TCP 服务端示例
 *
 * 本示例展示如何使用 ModbusServer 创建 Modbus TCP 服务器，
 * 模拟多个从机设备，支持各种寄存器的读写操作。
 *
 * 编译: g++ -std=c++20 -o tcp_server tcp_server.cpp \
 *       -I../src/net/protocol/modbus \
 *       -I../3rd/libmodbuspp/include \
 *       -L../build/3rd/libmodbuspp -lmodbuspp \
 *       -lpthread
 *
 * 运行: ./tcp_server [Port]
 *       默认: ./tcp_server 1502
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <random>
#include <cmath>
#include <algorithm>
#include "modbus_server.h"

using namespace ems::net::modbus;

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    g_running = false;
}

void printBanner() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           Modbus TCP Server Example (libmodbuspp)            ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  功能: 模拟 Modbus TCP 服务器，支持多个从机                   ║\n";
    std::cout << "║  从机1: 温度传感器 (模拟数据)                                  ║\n";
    std::cout << "║  从机2: 电能表 (计数器递增)                                   ║\n";
    std::cout << "║  从机3: 开关控制器 (可读写线圈)                               ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
}

class DataSimulator {
public:
    static float generateTemperature(float base = 25.0f, float variation = 2.0f) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<> d{0, 1};
        return base + d(gen) * variation;
    }

    static float generateHumidity(float base = 60.0f, float variation = 5.0f) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<> d{0, 1};
        float value = base + d(gen) * variation;
        return std::max(0.0f, std::min(100.0f, value));
    }

    static float generateVoltage(float base = 220.0f, float variation = 2.0f) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<> d{0, 1};
        return base + d(gen) * variation;
    }

    static float generateCurrent(float base = 10.0f, float variation = 1.0f) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<float> d{0, 1};
        return std::max(0.0f, base + d(gen) * variation);
    }

    static uint32_t generatePower(float base = 2200.0f, float variation = 100.0f) {
        static std::mt19937 gen{std::random_device{}()};
        static std::normal_distribution<float> d{0, 1};
        return static_cast<uint32_t>(std::max(0.0f, base + d(gen) * variation));
    }
};

void updateTemperatureSensor(ModbusServer& server, int slave_id) {
    float temp = DataSimulator::generateTemperature(25.0f, 1.0f);
    float humidity = DataSimulator::generateHumidity(60.0f, 3.0f);

    Modbus::Data<float, Modbus::EndianBig> temp_data(temp);
    Modbus::Data<float, Modbus::EndianBig> hum_data(humidity);

    server.writeHoldingRegisters(slave_id, 1, temp_data.registers().data(), 2);
    server.writeHoldingRegisters(slave_id, 3, hum_data.registers().data(), 2);

    std::cout << "\r[从机" << slave_id << "] 温度: " << std::fixed << std::setprecision(2)
              << temp << " °C, 湿度: " << humidity << " %     " << std::flush;
}

void updatePowerMeter(ModbusServer& server, int slave_id) {
    static uint32_t energy_counter = 0;
    static auto last_update = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
    last_update = now;

    energy_counter += static_cast<uint32_t>(elapsed * 100 / 3600000.0);

    Modbus::Data<uint32_t, Modbus::EndianBig> energy_data(energy_counter);
    server.writeHoldingRegisters(slave_id, 1, energy_data.registers().data(), 2);

    float voltage = DataSimulator::generateVoltage();
    float current = DataSimulator::generateCurrent();
    uint32_t power = static_cast<uint32_t>(voltage * current);

    Modbus::Data<float, Modbus::EndianBig> v_data(voltage);
    Modbus::Data<float, Modbus::EndianBig> i_data(current);
    Modbus::Data<uint32_t, Modbus::EndianBig> p_data(power);

    server.writeHoldingRegisters(slave_id, 3, v_data.registers().data(), 2);
    server.writeHoldingRegisters(slave_id, 5, i_data.registers().data(), 2);
    server.writeHoldingRegisters(slave_id, 7, p_data.registers().data(), 2);

    std::cout << "\r[从机" << slave_id << "] 电能: " << energy_counter << " kWh, "
              << "功率: " << power << " W             " << std::flush;
}

void updateSwitchController(ModbusServer& server, int slave_id) {
    static bool relay_states[8] = {false, false, false, false, false, false, false, false};
    static auto last_toggle = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_toggle).count() >= 5) {
        last_toggle = now;
        int idx = rand() % 8;
        relay_states[idx] = !relay_states[idx];
    }

    server.writeCoils(slave_id, 0, relay_states, 8);

    std::cout << "\r[从机" << slave_id << "] 继电器: ";
    for (int i = 0; i < 8; i++) {
        std::cout << (relay_states[i] ? "[●]" : "[○]");
    }
    std::cout << "      " << std::flush;
}

void setupServer(ModbusServer& server) {
    std::cout << "配置从机...\n";

    server.addSlave(1);
    server.setHoldingRegisterBlock(1, 0, 10);
    server.setInputRegisterBlock(1, 0, 10);
    std::cout << "  从机1 (ID=1): 温度传感器 - 寄存器 [0-9]\n";

    server.addSlave(2);
    server.setHoldingRegisterBlock(2, 0, 10);
    server.setInputRegisterBlock(2, 0, 10);
    std::cout << "  从机2 (ID=2): 电能表 - 寄存器 [0-9]\n";

    server.addSlave(3);
    server.setCoilBlock(3, 0, 16);
    server.setHoldingRegisterBlock(3, 0, 10);
    std::cout << "  从机3 (ID=3): 开关控制器 - 线圈 [0-15], 寄存器 [0-9]\n";

    server.setDataChangeCallback([](int sid, Modbus::Table table, int addr, int nb) {
        std::cout << "\n[数据变更回调] 从机" << sid
                  << " " << (table == Modbus::Table::HoldingRegister ? "HR" : "CO")
                  << " [" << addr << "-" << addr + nb - 1 << "]\n";
    });

    std::cout << "从机配置完成!\n\n";
}

bool demoManualOperations(ModbusServer& server) {
    std::cout << "\n\n========== 手动操作测试 ==========\n";

    std::cout << "\n[1] 写入保持寄存器\n";
    server.writeHoldingRegister(1, 10, 12345);
    uint16_t read_val;
    server.readHoldingRegister(1, 10, read_val);
    std::cout << "  从机1 [10] = " << read_val << "\n";

    std::cout << "\n[2] 写入浮点数\n";
    Modbus::Data<float, Modbus::EndianBig> setpoint(30.0f);
    server.writeHoldingRegisters(1, 20, setpoint.registers().data(), 2);
    std::cout << "  设置温度目标值: " << setpoint.value() << " °C\n";

    std::cout << "\n[3] 读取线圈\n";
    bool coils[8];
    server.readCoils(3, 0, coils, 8);
    std::cout << "  从机3 线圈 [0-7]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << (coils[i] ? "1" : "0");
    }
    std::cout << "\n";

    std::cout << "\n[4] 写入线圈\n";
    bool new_coils[] = {true, false, true, false, true, false, true, false};
    server.writeCoils(3, 0, new_coils, 8);
    std::cout << "  写入: ";
    for (int i = 0; i < 8; i++) {
        std::cout << (new_coils[i] ? "1" : "0");
    }
    std::cout << "\n";

    std::cout << "\n[5] 多寄存器批量写入\n";
    uint16_t batch_data[] = {100, 200, 300, 400, 500};
    server.writeHoldingRegisters(2, 10, batch_data, 5);
    std::cout << "  批量写入 [2:10-14]: 100, 200, 300, 400, 500\n";

    std::cout << "\n[6] 读取并验证\n";
    uint16_t verify[5];
    server.readHoldingRegisters(2, 10, verify, 5);
    std::cout << "  验证读取: ";
    bool ok = true;
    for (int i = 0; i < 5; i++) {
        std::cout << verify[i];
        if (verify[i] != batch_data[i]) ok = false;
        if (i < 4) std::cout << ", ";
    }
    std::cout << (ok ? " [OK]\n" : " [FAIL]\n");

    std::cout << "\n========== 手动操作完成 ==========\n";

    return true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    int port = (argc > 1) ? std::atoi(argv[1]) : 1502;

    std::cout << "服务参数:\n";
    std::cout << "  监听地址: 0.0.0.0 (所有接口)\n";
    std::cout << "  监听端口: " << port << "\n\n";

    ModbusServer server(ModbusServer::NetType::TCP, "0.0.0.0", std::to_string(port));

    setupServer(server);

    std::cout << "正在启动 Modbus TCP 服务器...\n";
    if (!server.start()) {
        std::cerr << "启动失败!\n";
        return 1;
    }

    std::cout << "服务器已启动!\n";
    std::cout << "等待 Modbus 主站连接...\n\n";

    std::cout << "\n开始数据模拟 (按 Ctrl+C 停止)...\n";
    std::cout << std::string(70, '-') << "\n";

    auto last_manual_demo = std::chrono::steady_clock::now();

    while (g_running.load()) {
        updateTemperatureSensor(server, 1);
        updatePowerMeter(server, 2);
        updateSwitchController(server, 3);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_manual_demo).count() >= 30) {
            last_manual_demo = now;
            demoManualOperations(server);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n\n正在停止服务器...\n";
    server.stop();

    std::cout << "服务器已停止.\n";
    return 0;
}
