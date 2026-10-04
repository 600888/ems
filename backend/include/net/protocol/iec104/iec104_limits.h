#pragma once

#include "net/protocol/enum_value.h"

namespace ems::net::iec104 {

/** @brief IEC104 默认配置；APCI 超时单位为秒。 */
enum class Defaults : int {
    Port = 2404,
    TimeoutMs = 3000,
    CommandTimeoutMs = 3000,
    StaleMs = 30000,
    MaxPoints = 65536,
    MaxSessions = 8,
    QueueSize = 1024,
    SendWindow = 12,
    ReceiveWindow = 8,
    ConnectionTimeoutSeconds = 10,
    MessageTimeoutSeconds = 15,
    ConfirmIntervalSeconds = 10,
    KeepAliveSeconds = 20,
    ConnectPollMs = 5,
};

/** @brief IEC104 地址和运行配置范围。 */
enum class Limits : int {
    MaxPort = 65535,
    MaxCommonAddress = 65534,
    MaxInformationAddress = 16777215,
    MaxTypeId = 255,
    MaxOriginator = 255,
    MaxQualityByte = 255,
    MaxHostBytes = 255,
    MaxStations = 256,
    MaxPoints = 65536,
    MaxWindow = 32767,
    MaxConnectionTimeoutSeconds = 60,
    MaxMessageTimeoutSeconds = 120,
    MaxKeepAliveSeconds = 3600,
    MaxTimeoutMs = 60000,
    MaxStaleMs = 86400000,
    MaxSessions = 100,
    MinQueueSize = 16,
    MaxQueueSize = 65536,
    MaxQualifier = 3,
    MinTimezoneMinutes = -840,
    MaxTimezoneMinutes = 840,
};

/** @brief CP56/CP24 时间编码范围及 UTC 毫秒边界。 */
enum class TimeEncoding : long long {
    MonthsPerYear = 12,
    LastYear = 99,
    February = 2,
    LeapYearCycle = 4,
    Epoch2000Ms = 946684800000,
    Epoch2100Ms = 4102444800000,
};

}  // namespace ems::net::iec104
