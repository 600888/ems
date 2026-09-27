/**
 * @file device_models.cpp
 * @brief PCS、BMS 与消防设备各自的固有点和能力目录。
 */
#include <common/model/device_models.h>

#include <utility>

namespace ems::model {
namespace {

void add_point(DeviceTypeDefinition& type, const char* code, PointKind kind, ValueType value_type,
               const char* unit = "") {
    type.standard_points.push_back({code, value_type, unit, kind});
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
    add_point(type, "run_state", PointKind::Status, ValueType::String);
    add_point(type, "control_mode", PointKind::Status, ValueType::String);
    add_point(type, "fault_active", PointKind::Status, ValueType::Bool);
    add_point(type, "fault_code", PointKind::Status, ValueType::String);
    add_point(type, "fault_event", PointKind::Event, ValueType::String);
    add_point(type, "active_power", PointKind::Telemetry, ValueType::Double, "kW");
    add_point(type, "reactive_power", PointKind::Telemetry, ValueType::Double, "kvar");
    add_point(type, "active_power_setpoint", PointKind::Telemetry, ValueType::Double, "kW");
    add_point(type, "reactive_power_setpoint", PointKind::Telemetry, ValueType::Double, "kvar");
    add_point(type, "dc_voltage", PointKind::Telemetry, ValueType::Double, "V");
    add_point(type, "dc_current", PointKind::Telemetry, ValueType::Double, "A");
    for (const char* code : {"ac_voltage_a", "ac_voltage_b", "ac_voltage_c", "ac_voltage_ab",
                             "ac_voltage_bc", "ac_voltage_ca"}) {
        add_point(type, code, PointKind::Telemetry, ValueType::Double, "V");
    }
    for (const char* code : {"ac_current_a", "ac_current_b", "ac_current_c"}) {
        add_point(type, code, PointKind::Telemetry, ValueType::Double, "A");
    }
    add_point(type, "frequency", PointKind::Telemetry, ValueType::Double, "Hz");
    add_point(type, "max_charge_power", PointKind::Telemetry, ValueType::Double, "kW");
    add_point(type, "max_discharge_power", PointKind::Telemetry, ValueType::Double, "kW");
    add_point(type, "start", PointKind::Command, ValueType::Bool);
    add_point(type, "stop", PointKind::Command, ValueType::Bool);
    add_point(type, "fault_reset", PointKind::Command, ValueType::Bool);
    add_point(type, "set_active_power", PointKind::Command, ValueType::Double, "kW");
    add_point(type, "set_reactive_power", PointKind::Command, ValueType::Double, "kvar");
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
    add_point(type, "soc", PointKind::Telemetry, ValueType::Double, "%");
    add_point(type, "soh", PointKind::Telemetry, ValueType::Double, "%");
    add_point(type, "pack_voltage", PointKind::Telemetry, ValueType::Double, "V");
    add_point(type, "pack_current", PointKind::Telemetry, ValueType::Double, "A");
    add_point(type, "max_charge_current", PointKind::Telemetry, ValueType::Double, "A");
    add_point(type, "max_discharge_current", PointKind::Telemetry, ValueType::Double, "A");
    add_point(type, "charge_allowed", PointKind::Status, ValueType::Bool);
    add_point(type, "discharge_allowed", PointKind::Status, ValueType::Bool);
    add_point(type, "cell_voltage_min", PointKind::Telemetry, ValueType::Double, "V");
    add_point(type, "cell_voltage_max", PointKind::Telemetry, ValueType::Double, "V");
    add_point(type, "cell_temperature_min", PointKind::Telemetry, ValueType::Double, "℃");
    add_point(type, "cell_temperature_max", PointKind::Telemetry, ValueType::Double, "℃");
    add_point(type, "contactor_state", PointKind::Status, ValueType::String);
    add_point(type, "fault_active", PointKind::Status, ValueType::Bool);
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
    add_point(type, "fire_alarm", PointKind::Status, ValueType::Bool);
    add_point(type, "fault_active", PointKind::Status, ValueType::Bool);
    add_point(type, "smoke_alarm", PointKind::Status, ValueType::Bool);
    add_point(type, "temperature_alarm", PointKind::Status, ValueType::Bool);
    add_point(type, "discharge_state", PointKind::Status, ValueType::String);
    add_capability_codes(type, capabilities());
    return type;
}

std::vector<StandardCapability> FireDeviceModel::capabilities() const {
    return {{"monitor_fire", {"fire_alarm", "fault_active"}}};
}

}  // namespace ems::model
