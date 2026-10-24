/**
 * @file device_models.cpp
 * @brief PCS、BMS 与消防设备各自的固有点和能力目录。
 *
 * 固有点以电力四遥（PointType）声明，记录种类由 to_point_kind 推导：
 * 遥控与遥调都映射为 Command，但保留各自的四遥分类供写入规则与界面使用。
 */
#include <common/model/device_models.h>

#include <utility>

namespace ems::model {
namespace {

void add_point(DeviceTypeDefinition& type, const char* code, PointType point_type,
               ValueType value_type, const char* unit = "") {
    type.standard_points.push_back({code, value_type, unit, to_point_kind(point_type), point_type});
}

void add_capability_codes(DeviceTypeDefinition& type,
                          const std::vector<StandardCapability>& capabilities) {
    for (const auto& capability : capabilities) {
        type.capability_codes.push_back(capability.code);
    }
}

}  // namespace

DeviceModel::DeviceModel(std::string type_code, std::string type_version_id, std::string name)
    : type_code_(std::move(type_code)),
      type_version_id_(std::move(type_version_id)),
      name_(std::move(name)) {}

PcsDeviceModel::PcsDeviceModel() : DeviceModel("pcs", "pcs-v1", "储能变流器") {}

DeviceTypeDefinition PcsDeviceModel::definition() const {
    DeviceTypeDefinition type;
    type.type_version_id = type_version_id();
    type.type_code = type_code();
    type.name = name();
    add_point(type, "run_state", PointType::YX, ValueType::String);
    add_point(type, "control_mode", PointType::YX, ValueType::String);
    add_point(type, "fault_active", PointType::YX, ValueType::Bool);
    add_point(type, "fault_code", PointType::YX, ValueType::String);
    add_point(type, "fault_event", PointType::YX, ValueType::String);
    add_point(type, "active_power", PointType::YC, ValueType::Double, "kW");
    add_point(type, "reactive_power", PointType::YC, ValueType::Double, "kvar");
    add_point(type, "active_power_setpoint", PointType::YC, ValueType::Double, "kW");
    add_point(type, "reactive_power_setpoint", PointType::YC, ValueType::Double, "kvar");
    add_point(type, "dc_voltage", PointType::YC, ValueType::Double, "V");
    add_point(type, "dc_current", PointType::YC, ValueType::Double, "A");
    for (const char* code : {"ac_voltage_a", "ac_voltage_b", "ac_voltage_c", "ac_voltage_ab",
                             "ac_voltage_bc", "ac_voltage_ca"}) {
        add_point(type, code, PointType::YC, ValueType::Double, "V");
    }
    for (const char* code : {"ac_current_a", "ac_current_b", "ac_current_c"}) {
        add_point(type, code, PointType::YC, ValueType::Double, "A");
    }
    add_point(type, "frequency", PointType::YC, ValueType::Double, "Hz");
    add_point(type, "max_charge_power", PointType::YC, ValueType::Double, "kW");
    add_point(type, "max_discharge_power", PointType::YC, ValueType::Double, "kW");
    add_point(type, "start", PointType::YK, ValueType::Bool);
    add_point(type, "stop", PointType::YK, ValueType::Bool);
    add_point(type, "fault_reset", PointType::YK, ValueType::Bool);
    add_point(type, "set_active_power", PointType::YT, ValueType::Double, "kW");
    add_point(type, "set_reactive_power", PointType::YT, ValueType::Double, "kvar");
    add_capability_codes(type, capabilities());
    return type;
}

std::vector<StandardCapability> PcsDeviceModel::capabilities() const {
    return {{"monitor_power", {"active_power"}},
            {"start_stop", {"start", "stop", "run_state", "control_mode"}},
            {"set_active_power", {"set_active_power", "active_power", "control_mode"}},
            {"set_reactive_power", {"set_reactive_power", "reactive_power", "control_mode"}}};
}

BmsDeviceModel::BmsDeviceModel() : DeviceModel("bms", "bms-v1", "电池管理系统") {}

DeviceTypeDefinition BmsDeviceModel::definition() const {
    DeviceTypeDefinition type;
    type.type_version_id = type_version_id();
    type.type_code = type_code();
    type.name = name();
    add_point(type, "soc", PointType::YC, ValueType::Double, "%");
    add_point(type, "soh", PointType::YC, ValueType::Double, "%");
    add_point(type, "pack_voltage", PointType::YC, ValueType::Double, "V");
    add_point(type, "pack_current", PointType::YC, ValueType::Double, "A");
    add_point(type, "max_charge_current", PointType::YC, ValueType::Double, "A");
    add_point(type, "max_discharge_current", PointType::YC, ValueType::Double, "A");
    add_point(type, "charge_allowed", PointType::YX, ValueType::Bool);
    add_point(type, "discharge_allowed", PointType::YX, ValueType::Bool);
    add_point(type, "cell_voltage_min", PointType::YC, ValueType::Double, "V");
    add_point(type, "cell_voltage_max", PointType::YC, ValueType::Double, "V");
    add_point(type, "cell_temperature_min", PointType::YC, ValueType::Double, "℃");
    add_point(type, "cell_temperature_max", PointType::YC, ValueType::Double, "℃");
    add_point(type, "contactor_state", PointType::YX, ValueType::String);
    add_point(type, "fault_active", PointType::YX, ValueType::Bool);
    add_capability_codes(type, capabilities());
    return type;
}

std::vector<StandardCapability> BmsDeviceModel::capabilities() const {
    return {{"read_soc", {"soc"}},
            {"monitor_battery", {"soc", "pack_voltage", "pack_current", "fault_active"}}};
}

FireDeviceModel::FireDeviceModel() : DeviceModel("fire", "fire-v1", "消防设备") {}

DeviceTypeDefinition FireDeviceModel::definition() const {
    DeviceTypeDefinition type;
    type.type_version_id = type_version_id();
    type.type_code = type_code();
    type.name = name();
    add_point(type, "fire_alarm", PointType::YX, ValueType::Bool);
    add_point(type, "fault_active", PointType::YX, ValueType::Bool);
    add_point(type, "smoke_alarm", PointType::YX, ValueType::Bool);
    add_point(type, "temperature_alarm", PointType::YX, ValueType::Bool);
    add_point(type, "discharge_state", PointType::YX, ValueType::String);
    add_capability_codes(type, capabilities());
    return type;
}

std::vector<StandardCapability> FireDeviceModel::capabilities() const {
    return {{"monitor_fire", {"fire_alarm", "fault_active"}}};
}

}  // namespace ems::model
