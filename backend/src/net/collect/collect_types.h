#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::collect {

/** @brief 采集引擎生命周期状态。 */
enum class EngineState : int {
    Idle = 0,
    Running = 1,
    Stopped = 2,
    Error = 3,
};
/** @brief 采集通道生命周期状态。 */
enum class ChannelState : int {
    Created = 0,
    Starting = 1,
    Running = 2,
    Stopping = 3,
    Stopped = 4,
    Error = 5,
};
/** @brief 测点变更通知的传输后端。 */
enum class NotificationBackend : int {
    Redis = 0,
    ZeroMQ = 1,
    None = 2,
};

}  // namespace ems::net::collect
