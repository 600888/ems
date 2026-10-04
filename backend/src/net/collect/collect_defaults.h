#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::collect {

/** @brief 采集调度默认值；时间单位为毫秒。 */
enum class CollectDefaults : int {
    PollIntervalMs = 1000,
    MinPollIntervalMs = 100,
    CommandTimeoutMs = 5000,
    MaxChannels = 128,
    MaxRetries = 5,
    ReconnectBaseDelayMs = 1000,
    ReconnectMaxDelayMs = 60000,
    ReconnectMaxExponent = 6,
};
/** @brief 数据分发默认值；容量为字节，时间为毫秒。 */
enum class DispatchDefaults : int {
    RedisPort = 6379,
    RedisPoolSize = 4,
    RedisTimeoutMs = 3000,
    ZmqQueueItems = 1024,
    ZmqMaxMessageBytes = 65536,
    ZmqHighWaterMark = 1024,
    BatchMaxSize = 500,
    BatchIntervalMs = 100,
};

}  // namespace ems::net::collect
