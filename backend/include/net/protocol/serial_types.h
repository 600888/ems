#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::protocol {

/** @brief 串口默认参数，波特率单位为 bit/s。 */
enum class SerialDefaults : int {
    BaudRate = 9600,
    DataBits = 8,
    StopBits = 1,
};

/** @brief 串口参数合法范围。 */
enum class SerialLimits : int {
    MinBaudRate = 300,
    MaxBaudRate = 115200,
    MinDataBits = 5,
    MaxDataBits = 8,
    MinStopBits = 1,
    MaxStopBits = 2,
};

}  // namespace ems::net::protocol
