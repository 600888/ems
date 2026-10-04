#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::modbus {

/** @brief Modbus 地址、请求和解析限制。 */
enum class Limits : int {
    MaxSlaveId = 247,
    MaxAddress = 65535,
    MaxTcpPort = 65535,
    AddressCount = 65536,
    MaxReadCoils = 2000,
    MaxReadRegisters = 125,
    MaxBitOffset = 15,
    MinPointFields = 3,
    MaxPointFields = 6,
    MaxLegacyTimeoutSeconds = 60,
    MaxTimeoutMs = 60000,
    LegacyBatchBufferRegisters = 256,
};

/** @brief Modbus 运行默认参数；时间单位在名称中标明。 */
enum class Defaults : int {
    TcpPort = 502,
    SlaveId = 1,
    ResponseTimeoutMs = 1000,
    ByteTimeoutMs = 1000,
    ImageSize = 1024,
    MaxSessions = 16,
    IoWaitUs = 100000,
    ServerPollMs = 100,
    CaptureMessageLimit = 100,
};

/** @brief Modbus 寄存器与编码的位宽和容量。 */
enum class Layout : int {
    BytesPerRegister = 2,
    BitsPerByte = 8,
    BitsPerRegister = 16,
    ByteMask = 255,
    LeastSignificantBit = 1,
    SingleRegisters = 1,
    DoubleRegisters = 2,
    QuadRegisters = 4,
    MaxValueBytes = 8,
    LegacyScratchBytes = 16,
};

}  // namespace ems::net::modbus
