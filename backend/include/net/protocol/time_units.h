#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::protocol {
/** @brief 时间单位换算倍率。 */
enum class TimeUnits : int {
    MillisecondsPerSecond = 1000,
    MicrosecondsPerMillisecond = 1000,
    MillisecondsPerMinute = 60000,
    SecondsPerMinute = 60,
    HoursPerDay = 24,
};

}  // namespace ems::net::protocol
