/**
 * @file rtdb_ems_usage.cpp
 * @brief EMS工商储实时数据库使用示例
 */
#include <chrono>
#include <iostream>
#include <thread>

#include "rtdb/rtdb_ems.h"

using namespace ems::net;

void example_device_snapshot() {
    std::cout << "\n=== 设备实时快照示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    config.prefix = "ems:";

    auto rtdb = createRTDBEMS(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Redis连接失败: " << rtdb->getLastError() << std::endl;
        return;
    }

    std::cout << "✓ Redis连接成功" << std::endl;

    DeviceSnapshot pcs_snapshot;
    pcs_snapshot.device_id = "pcs_001";
    pcs_snapshot.device_type = "PCS";
    pcs_snapshot.timestamp = std::time(nullptr);
    pcs_snapshot.quality = 0;

    pcs_snapshot.yc_values["voltage_a"] = 220.5;
    pcs_snapshot.yc_values["voltage_b"] = 221.0;
    pcs_snapshot.yc_values["voltage_c"] = 219.8;
    pcs_snapshot.yc_values["current_a"] = 45.2;
    pcs_snapshot.yc_values["current_b"] = 45.5;
    pcs_snapshot.yc_values["current_c"] = 44.8;
    pcs_snapshot.yc_values["power"] = 29.5;
    pcs_snapshot.yc_values["soc"] = 75.5;

    pcs_snapshot.yx_values["running"] = 1;
    pcs_snapshot.yx_values["charging"] = 0;
    pcs_snapshot.yx_values["discharging"] = 1;
    pcs_snapshot.yx_values["fault"] = 0;

    pcs_snapshot.metadata["manufacturer"] = "华为";
    pcs_snapshot.metadata["model"] = "SUN2000-100KTL";
    pcs_snapshot.metadata["firmware"] = "V100";

    if (rtdb->setDeviceSnapshot("pcs_001", pcs_snapshot)) {
        std::cout << "✓ PCS设备快照保存成功" << std::endl;
    }

    DeviceSnapshot bms_snapshot;
    bms_snapshot.device_id = "bms_001";
    bms_snapshot.device_type = "BMS";
    bms_snapshot.timestamp = std::time(nullptr);
    bms_snapshot.quality = 0;

    bms_snapshot.yc_values["total_voltage"] = 1024.5;
    bms_snapshot.yc_values["total_current"] = 28.5;
    bms_snapshot.yc_values["soc"] = 75.5;
    bms_snapshot.yc_values["soh"] = 98.5;
    bms_snapshot.yc_values["max_temp"] = 35.2;
    bms_snapshot.yc_values["min_temp"] = 32.1;

    bms_snapshot.yx_values["charging"] = 0;
    bms_snapshot.yx_values["discharging"] = 1;
    bms_snapshot.yx_values["balancing"] = 0;
    bms_snapshot.yx_values["alarm"] = 0;

    if (rtdb->setDeviceSnapshot("bms_001", bms_snapshot)) {
        std::cout << "✓ BMS设备快照保存成功" << std::endl;
    }

    DeviceSnapshot read_snapshot;
    if (rtdb->getDeviceSnapshot("pcs_001", read_snapshot)) {
        std::cout << "\n读取PCS设备快照:" << std::endl;
        std::cout << "  设备ID: " << read_snapshot.device_id << std::endl;
        std::cout << "  设备类型: " << read_snapshot.device_type << std::endl;
        std::cout << "  SOC: " << read_snapshot.yc_values.at("soc") << "%" << std::endl;
        std::cout << "  功率: " << read_snapshot.yc_values.at("power") << " kW" << std::endl;
        std::cout << "  运行状态: " << (read_snapshot.yx_values.at("running") ? "运行" : "停止")
                  << std::endl;
        std::cout << "  放电状态: " << (read_snapshot.yx_values.at("discharging") ? "放电" : "充电")
                  << std::endl;
    }

    double soc;
    if (rtdb->getDevicePoint("pcs_001", "soc", soc)) {
        std::cout << "\n当前SOC: " << soc << "%" << std::endl;
    }

    rtdb->updateDevicePoint("pcs_001", "power", 30.5);
    std::cout << "✓ 功率更新成功" << std::endl;
}

void example_control_command() {
    std::cout << "\n=== 控制指令队列示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDBEMS(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Redis连接失败" << std::endl;
        return;
    }

    DeviceCommand cmd;
    cmd.command_id = "cmd_001";
    cmd.device_id = "pcs_001";
    cmd.command_type = "set_power";
    cmd.point_code = "power";
    cmd.value = 25.0;
    cmd.priority = 1;
    cmd.timestamp = std::time(nullptr);
    cmd.source = "strategy_server";

    if (rtdb->pushCommand(cmd)) {
        std::cout << "✓ 控制指令已下发" << std::endl;
        std::cout << "  设备: " << cmd.device_id << std::endl;
        std::cout << "  命令类型: " << cmd.command_type << std::endl;
        std::cout << "  目标功率: " << cmd.value << " kW" << std::endl;
        std::cout << "  来源: " << cmd.source << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    CommandResult result;
    result.success = true;
    result.message = "功率设置成功";
    result.timestamp = std::time(nullptr);
    result.error_code = 0;

    if (rtdb->ackCommand(cmd.command_id, result.success, result.message)) {
        std::cout << "✓ 指令执行结果已确认" << std::endl;
    }
}

void example_device_lock() {
    std::cout << "\n=== 设备控制互斥锁示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDBEMS(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Redis连接失败" << std::endl;
        return;
    }

    std::string device_id = "pcs_001";
    std::string owner_1 = "strategy_peak";
    std::string owner_2 = "strategy_valley";

    if (rtdb->acquireControlLock(device_id, owner_1, 10)) {
        std::cout << "✓ 策略1获取设备控制权" << std::endl;
    }

    if (rtdb->isLocked(device_id)) {
        std::cout << "  设备已被锁定" << std::endl;
        std::cout << "  锁定者: " << rtdb->getLockOwner(device_id) << std::endl;
    }

    if (!rtdb->acquireControlLock(device_id, owner_2, 5)) {
        std::cout << "✓ 策略2尝试获取锁失败（设备正被策略1控制）" << std::endl;
    }

    bool operation_success = rtdb->executeWithLock(
        device_id, owner_1,
        []() {
            std::cout << "  执行削峰策略: 降低功率到20kW" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return true;
        },
        5000);

    if (operation_success) {
        std::cout << "✓ 策略1执行完成，释放控制权" << std::endl;
    }

    rtdb->releaseControlLock(device_id, owner_1);
    std::cout << "✓ 策略1释放设备控制权" << std::endl;

    if (rtdb->acquireControlLock(device_id, owner_2, 10)) {
        std::cout << "✓ 策略2获取设备控制权成功" << std::endl;

        rtdb->executeWithLock(
            device_id, owner_2,
            []() {
                std::cout << "  执行谷时策略: 充电功率设置为40kW" << std::endl;
                return true;
            },
            5000);

        rtdb->releaseControlLock(device_id, owner_2);
    }
}

void example_time_series() {
    std::cout << "\n=== 时序数据缓冲示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDBEMS(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Redis连接失败" << std::endl;
        return;
    }

    std::string device_id = "pcs_001";
    std::string point_code = "power";

    uint64_t base_time = std::time(nullptr) - 3600;

    for (int i = 0; i < 10; ++i) {
        double power = 25.0 + (i % 5) * 2.0;
        rtdb->appendTimeSeriesData(device_id, point_code, power, base_time + i * 360);
    }

    std::cout << "✓ 时序数据写入成功（10个数据点）" << std::endl;

    auto history = rtdb->getTimeSeriesData(device_id, point_code, 0, 0, 5);
    std::cout << "\n读取最近5个数据点:" << std::endl;
    for (const auto& [timestamp, value] : history) {
        std::cout << "  " << timestamp << ": " << value << " kW" << std::endl;
    }
}

void example_topology() {
    std::cout << "\n=== 系统拓扑缓存示例 ===" << std::endl;

    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";

    auto rtdb = createRTDBEMS(config);
    if (!rtdb || !rtdb->isConnected()) {
        std::cerr << "Redis连接失败" << std::endl;
        return;
    }

    SystemTopology topology;
    topology.topology_id = "topo_001";
    topology.name = "工商储系统拓扑";

    topology.device_ids = {"pcs_001", "bms_001", "meter_001"};

    topology.relationships["pcs_001"] = "bms_001";
    topology.relationships["pcs_001"] = "meter_001";
    topology.relationships["bms_001"] = "meter_001";

    topology.metadata["site_name"] = "工业园区A";
    topology.metadata["capacity"] = "500kWh";
    topology.metadata["rated_power"] = "100kW";

    if (rtdb->setTopology(topology)) {
        std::cout << "✓ 系统拓扑保存成功" << std::endl;
    }

    SystemTopology read_topo;
    if (rtdb->getTopology(topology.topology_id, read_topo)) {
        std::cout << "\n读取系统拓扑:" << std::endl;
        std::cout << "  拓扑ID: " << read_topo.topology_id << std::endl;
        std::cout << "  名称: " << read_topo.name << std::endl;
        std::cout << "  设备列表:" << std::endl;
        for (const auto& device_id : read_topo.device_ids) {
            std::cout << "    - " << device_id << std::endl;
        }
        std::cout << "  站点名称: " << read_topo.metadata.at("site_name") << std::endl;
        std::cout << "  装机容量: " << read_topo.metadata.at("capacity") << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== EMS工商储实时数据库示例 ===" << std::endl;

    example_device_snapshot();
    example_control_command();
    example_device_lock();
    example_time_series();
    example_topology();

    std::cout << "\n=== 所有示例执行完成 ===" << std::endl;

    return 0;
}
