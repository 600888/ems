#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::protocol {

/** @brief 配置数值文本支持的进制。 */
enum class NumberBase : int { Decimal = 10, Hexadecimal = 16 };

/** @brief 带 0x 前缀的数值文本布局。 */
enum class NumericText : int { HexPrefixLength = 2 };

}  // namespace ems::net::protocol
