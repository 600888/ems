#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::device {

/** @brief 旧数据库连接类型编码；与运行时 ConnectionType 的编码独立。 */
enum class DatabaseConnectionType : int { Serial = 1, Tcp = 2 };

/** @brief 数据导入时间文本缓冲容量，包含结束符。 */
enum class ImportLimits : int { TimestampTextBytes = 32 };

}  // namespace ems::net::device
