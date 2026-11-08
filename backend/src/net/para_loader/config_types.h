#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::device {

/** @brief 工程配置连接类型编码：串口为 1，TCP 为 2。 */
enum class DatabaseConnectionType : int { Serial = 1, Tcp = 2 };

}  // namespace ems::net::device
