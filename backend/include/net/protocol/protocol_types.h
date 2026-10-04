#pragma once

#include "net/protocol/enum_value.h"
#include "net/protocol/protocol_plugin_api.h"

namespace ems::net::protocol {

/** @brief 插件操作结果；非负批量返回值仍表示成功点数。 */
enum class OperationResult : int {
    Success = 0,
    Failure = -1,
    BufferTooSmall = -2,
};

/** @brief 插件连接查询返回值。 */
enum class ConnectionState : int {
    Disconnected = 0,
    Connected = 1,
};

/** @brief 采集插件品质码，与存量 RTDB PointQuality 保持兼容。 */
enum class SampleQuality : int {
    Good = 0,
    Invalid = 1,
    Reserved = 2,
    Error = 3,
};

/** @brief 所有协议共用的批量和诊断缓冲限制，容量单位为字节。 */
enum class PluginLimits : int {
    MaxBatchPoints = 65536,
    ErrorBytes = 1024,
    InitialResultBytes = 65536,
    MaxResultBytes = 8388608,
    BufferGrowthFactor = 2,
};

static_assert(enum_value(OperationResult::BufferTooSmall) == EMS_PLUGIN_BUFFER_TOO_SMALL,
              "C++ result code must match the public C plugin ABI");
static_assert(enum_value(PluginLimits::InitialResultBytes) == EMS_PLUGIN_RESULT_BUF_SIZE,
              "C++ result capacity must match the public C plugin ABI");

}  // namespace ems::net::protocol
