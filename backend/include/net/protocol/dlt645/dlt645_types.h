#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::dlt645 {

/** @brief DL/T645 DI 最高字节的数据类别。 */
enum class DataClass : int {
    Energy = 0,
    Demand = 1,
    Variable = 2,
    Event = 3,
    Parameter = 4,
};

/** @brief DL/T645 控制字与异常应答标志。 */
enum class ControlCode : int {
    ReadData = 0x11,
    ReadAddress = 0x13,
    ReadDataError = 0xd1,
    ErrorResponseMask = 0xc0,
};

/** @brief DL/T645 异常应答数据字。 */
enum class ErrorCode : int {
    NoRequestedData = 0x02,
    AccessDenied = 0x04,
};

}  // namespace ems::net::dlt645
