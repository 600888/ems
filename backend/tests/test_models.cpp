/**
 * @file test_models.cpp
 * @brief 数据模型单元测试
 */
#include <iostream>
#include <cassert>
#include "models/models.h"

using namespace ems::model;

void test_device_group() {
    std::cout << "Testing DeviceGroup..." << std::endl;
    
    auto group = std::make_shared<DeviceGroup>();
    group->id = 1;
    group->code = "group_001";
    group->name = "测试设备组";
    group->description = "这是一个测试设备组";
    
    json j = group->to_json();
    std::cout << "DeviceGroup JSON: " << j.dump(2) << std::endl;
    
    auto group2 = std::make_shared<DeviceGroup>();
    group2->from_json(j);
    assert(group2->code == group->code);
    assert(group2->name == group->name);
    
    std::cout << "DeviceGroup test passed!" << std::endl << std::endl;
}

void test_device() {
    std::cout << "Testing Device..." << std::endl;
    
    auto device = std::make_shared<Device>();
    device->id = 1;
    device->code = "device_001";
    device->name = "测试设备";
    device->device_type = DeviceType::RTU;
    device->description = "这是一个RTU设备";
    
    json j = device->to_json();
    std::cout << "Device JSON: " << j.dump(2) << std::endl;
    
    auto device2 = std::make_shared<Device>();
    device2->from_json(j);
    assert(device2->code == device->code);
    assert(device2->get_device_type_name() == "RTU");
    
    std::cout << "Device test passed!" << std::endl << std::endl;
}

void test_channel() {
    std::cout << "Testing Channel..." << std::endl;
    
    auto channel = std::make_shared<Channel>();
    channel->id = 1;
    channel->code = "channel_001";
    channel->name = "Modbus TCP通道";
    channel->protocol_type = ProtocolType::ModbusTCP;
    channel->conn_type = ConnectionType::TCP;
    channel->ip = "192.168.1.100";
    channel->port = 502;
    channel->timeout = 5;
    
    json j = channel->to_json();
    std::cout << "Channel JSON: " << j.dump(2) << std::endl;
    
    auto channel2 = std::make_shared<Channel>();
    channel2->from_json(j);
    assert(channel2->code == channel->code);
    assert(channel2->get_protocol_name() == "ModbusTCP");
    
    std::cout << "Channel test passed!" << std::endl << std::endl;
}

void test_point_yc() {
    std::cout << "Testing PointYC..." << std::endl;
    
    auto point = std::make_shared<PointYC>();
    point->id = 1;
    point->code = "voltage_a";
    point->name = "A相电压";
    point->reg_addr = "0";
    point->mul_coe = 0.1;
    point->add_coe = 0.0;
    point->max_limit = 250.0;
    point->min_limit = 190.0;
    point->unit = "V";
    
    json j = point->to_json();
    std::cout << "PointYC JSON: " << j.dump(2) << std::endl;
    
    auto point2 = std::make_shared<PointYC>();
    point2->from_json(j);
    assert(point2->code == point->code);
    assert(point2->transform_value(2200) == 220.0);
    assert(point2->is_value_valid(220.0));
    
    std::cout << "PointYC test passed!" << std::endl << std::endl;
}

void test_point_yx() {
    std::cout << "Testing PointYX..." << std::endl;
    
    auto point = std::make_shared<PointYX>();
    point->id = 1;
    point->code = "breaker_status";
    point->name = "断路器状态";
    point->reg_addr = "0";
    point->bit = 0;
    point->reverse = true;
    
    json j = point->to_json();
    std::cout << "PointYX JSON: " << j.dump(2) << std::endl;
    
    auto point2 = std::make_shared<PointYX>();
    point2->from_json(j);
    assert(point2->code == point->code);
    assert(point2->transform_value(false) == true);  // reverse=true
    
    std::cout << "PointYX test passed!" << std::endl << std::endl;
}

void test_point_yk() {
    std::cout << "Testing PointYK..." << std::endl;
    
    auto point = std::make_shared<PointYK>();
    point->id = 1;
    point->code = "yk_breaker_001";
    point->name = "断路器遥控";
    point->reg_addr = "0";
    point->command_type = YKCommandType::Pulse;
    
    json j = point->to_json();
    std::cout << "PointYK JSON: " << j.dump(2) << std::endl;
    
    auto point2 = std::make_shared<PointYK>();
    point2->from_json(j);
    assert(point2->code == point->code);
    assert(point2->get_command_type_name() == "Pulse");
    
    std::cout << "PointYK test passed!" << std::endl << std::endl;
}

void test_point_yt() {
    std::cout << "Testing PointYT..." << std::endl;
    
    auto point = std::make_shared<PointYT>();
    point->id = 1;
    point->code = "yt_tap_001";
    point->name = "分接头调节";
    point->reg_addr = "10";
    point->mul_coe = 0.01;
    point->min_limit = 0;
    point->max_limit = 100;
    
    json j = point->to_json();
    std::cout << "PointYT JSON: " << j.dump(2) << std::endl;
    
    auto point2 = std::make_shared<PointYT>();
    point2->from_json(j);
    assert(point2->code == point->code);
    assert(point2->is_value_valid(50.0));
    
    std::cout << "PointYT test passed!" << std::endl << std::endl;
}

void test_slave() {
    std::cout << "Testing Slave..." << std::endl;
    
    auto slave = std::make_shared<Slave>();
    slave->id = 1;
    slave->slave_id = 1;
    slave->name = "从机1";
    slave->description = "Modbus从机设备";
    
    json j = slave->to_json();
    std::cout << "Slave JSON: " << j.dump(2) << std::endl;
    
    auto slave2 = std::make_shared<Slave>();
    slave2->from_json(j);
    assert(slave2->slave_id == slave->slave_id);
    assert(slave2->name == slave->name);
    
    std::cout << "Slave test passed!" << std::endl << std::endl;
}

void test_point_mapping() {
    std::cout << "Testing PointMapping..." << std::endl;
    
    auto mapping = std::make_shared<PointMapping>();
    mapping->id = 1;
    mapping->device_name = "virtual_device";
    mapping->target_point_code = "power_factor";
    mapping->source_point_codes = {"active_power", "reactive_power"};
    mapping->formula = "sqrt(pow(active_power,2) + pow(reactive_power,2))";
    mapping->description = "计算功率因数";
    
    json j = mapping->to_json();
    std::cout << "PointMapping JSON: " << j.dump(2) << std::endl;
    
    std::cout << "Formula: " << mapping->to_formula_string() << std::endl;
    
    std::cout << "PointMapping test passed!" << std::endl << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "EMS Data Models Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        test_device_group();
        test_device();
        test_channel();
        test_point_yc();
        test_point_yx();
        test_point_yk();
        test_point_yt();
        test_slave();
        test_point_mapping();
        
        std::cout << "========================================" << std::endl;
        std::cout << "All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
