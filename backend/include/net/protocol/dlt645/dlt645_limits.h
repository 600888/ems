#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::dlt645 {

/** @brief 将工程值缩放为 BCD 整数时允许的浮点舍入误差。 */
inline constexpr double kDecimalTolerance = 1e-6;

/** @brief DL/T645 默认通信参数。 */
enum class Defaults : int {
    TcpPort = 8899,
    TimeoutMs = 3000,
    LegacyTimeoutSeconds = 3,
};

/** @brief DL/T645 解析与通信限制。 */
enum class Limits : int {
    MaxFieldIndex = 65535,
    MaxNumericDigits = 15,
    MaxTimeoutMs = 60000,
    MaxTimeoutSeconds = 60,
    MaxTcpPort = 65535,
};

/** @brief DL/T645 表地址、密码、DI 与 BCD 布局。 */
enum class Layout : int {
    MeterDigits = 12,
    MeterBytes = 6,
    PasswordBytes = 4,
    DiBytes = 4,
    DiClassShift = 24,
    BitsPerByte = 8,
    BcdDigitsPerByte = 2,
    BcdNibbleBits = 4,
    DiscoveryAddressByte = 0xaa,
};

}  // namespace ems::net::dlt645
