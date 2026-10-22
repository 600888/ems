#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::device {

/** @brief 工程配置连接类型编码；与运行时 ConnectionType 的编码独立。 */
enum class DatabaseConnectionType : int { Serial = 1, Tcp = 2 };

}  // namespace ems::net::device
