/**
 * @file base.h
 * @brief RTDB 与存量协议处理器共享的枚举和解码工具。
 */
#ifndef EMS_MODEL_BASE_H
#define EMS_MODEL_BASE_H

#include <nlohmann/json.hpp>

#include "net/protocol/modbus/modbus_types.h"

namespace ems {
namespace model {

using json = nlohmann::json;

enum class ProtocolType : int {
    ModbusRTU = 0,
    ModbusTCP = 1,
    IEC104 = 2,
    DLT645 = 3,
    IEC61850 = 4,
    OPCUA = 5,
    MQTT = 6,
    CAN = 7,
    DLT698 = 8,
    Other = 99
};

enum class DeviceType : int {
    // 通用设备类型
    RTU = 0,
    Protection = 1,
    Meter = 2,
    IED = 3,
    PLC = 4,
    Virtual = 5,
    Gateway = 6,
    // 储能/能源专用设备类型
    UNKNOWN = 10,
    INVERTER = 11,
    BATTERY = 12,
    BMS = 13,
    TRANSFORMER = 14,
    HVAC = 15,
    FIRE_CONTROL = 16,
    ENV_MONITOR = 17,
    Other = 99
};

enum class PointType : int { YC = 0, YX = 1, YK = 2, YT = 3 };

enum class YKCommandType : int { Pulse = 0, Latch = 1, Step = 2 };

/** @brief 存量模型沿用 Modbus 解码与端序类型，保持调用接口兼容。 */
using DecodeCode = net::modbus::DecodeCode;
using EndianCode = net::modbus::EndianCode;

/**
 * @brief 获取单寄存器类型的对应字节数
 * @param[in] code 对象编码。
 * @return 处理或统计得到的数据项数量。
 */
constexpr int decode_size(DecodeCode code) noexcept {
    switch (code) {
        case DecodeCode::UINT8:
        case DecodeCode::INT8:
        case DecodeCode::BOOL:
        case DecodeCode::BIT:
            return 1;
        case DecodeCode::UINT16:
        case DecodeCode::INT16:
            return 2;
        case DecodeCode::UINT32:
        case DecodeCode::INT32:
        case DecodeCode::FLOAT:
            return 4;
        case DecodeCode::DOUBLE:
            return 8;
    }
    return 2;
}

/**
 * @brief 获取解码类型对应的寄存器数量
 * @param[in] code 对象编码。
 * @return 处理或统计得到的数据项数量。
 */
constexpr int decode_reg_count(DecodeCode code) noexcept { return decode_size(code) / 2; }

/**
 * @brief 测点品质枚举
 */
enum class PointQuality : int { Good = 0, Invalid = 1, Reserved = 2, Error = 3 };

/**
 * @brief 遥控执行状态枚举
 */
enum class YKStatus : int { Idle = 0, Pending = 1, Executing = 2, Success = 3, Failed = 4 };

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_BASE_H
