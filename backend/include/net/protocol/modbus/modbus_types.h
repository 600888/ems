#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::modbus {

/** @brief 点编码各字段的位置：slave:function:offset[:decode[:endian[:bit]]]。 */
enum class PointField : int {
    Slave = 0,
    Function = 1,
    Address = 2,
    Decode = 3,
    Endian = 4,
    Bit = 5
};

/** @brief Modbus 功能码，保持线缆及点编码的标准数值。 */
enum class FunctionCode : int {
    ReadCoils = 1,
    ReadDiscreteInputs = 2,
    ReadHoldingRegisters = 3,
    ReadInputRegisters = 4,
    WriteSingleCoil = 5,
    WriteSingleRegister = 6,
    WriteMultipleCoils = 15,
    WriteMultipleRegisters = 16,
};

/**
 * @brief 解码方式枚举
 */
enum class DecodeCode : int {
    UINT16 = 0,
    INT16 = 1,
    UINT32 = 2,
    INT32 = 3,
    FLOAT = 4,
    DOUBLE = 5,
    UINT8 = 6,
    INT8 = 7,
    BOOL = 8,
    BIT = 9
};

/**
 * @brief 端序枚举
 *
 * 定义多寄存器数据类型（UINT32/INT32/FLOAT/DOUBLE）中
 * 16 位寄存器的排列顺序。
 *
 * 以 4 字节 (2 寄存器) FLOAT [R0][R1] 为例:
 *
 *     BigEndian              (0): R0高字节 R0低字节 R1高字节 R1低字节  (AB CD — Modbus标准)
 *     LittleEndian           (1): R1高字节 R1低字节 R0高字节 R0低字节  (CD AB — 字交换)
 *     BigEndianByteSwap      (2): R0低字节 R0高字节 R1低字节 R1高字节  (BA DC — 字节交换)
 *     LittleEndianByteSwap   (3): R1低字节 R1高字节 R0低字节 R0高字节(DC BA — 字+字节交换)
 *
 * @note 避免使用 UPPER_SNAKE_CASE 命名，防止与系统宏 (BIG_ENDIAN/LITTLE_ENDIAN) 冲突
 */
enum class EndianCode : int {
    BigEndian = 0,            // AB CD — Modbus 默认大端
    LittleEndian = 1,         // CD AB — 字交换
    BigEndianByteSwap = 2,    // BA DC — 字节交换
    LittleEndianByteSwap = 3  // DC BA — 字交换 + 字节交换
};

/** @brief Modbus 传输类型。 */
enum class NetType : int {
    RTU = 0,
    TCP = 1,
};
/** @brief 存量 Modbus 客户端操作错误码。 */
enum class ClientError : int {
    Success = 0,
    ConnectionFailed = -1,
    Timeout = -2,
    InvalidAddress = -3,
    InvalidData = -4,
    SlaveError = -5,
    Unknown = -99,
};

/** @brief 存量 Modbus 服务端操作错误码。 */
enum class ServerError : int {
    Success = 0,
    BindFailed = -1,
    StartFailed = -2,
    AlreadyRunning = -3,
    Unknown = -99,
};

}  // namespace ems::net::modbus
