#pragma once

#include <common/database/timeseries/types.h>

namespace ems::database {

struct PointSnapshot {
    std::string scope_kind;
    std::string scope_id;
    std::string config_version;
    std::string point_id;
    std::string owner_device_id;  ///< 点归属设备树节点
    std::string point_code;
    std::string display_name;
    std::string unit;
    ValueType value_type{ValueType::Numeric};
    std::string series_key;
    bool enabled{false};
};

struct QuarantineRecord {
    std::string stream_id;
    std::string scope;
    std::string event_id;
    std::string reason;
    std::string payload;
};

}  // namespace ems::database
