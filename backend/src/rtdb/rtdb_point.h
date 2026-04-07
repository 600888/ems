#ifndef EMS_RTDB_POINT_H
#define EMS_RTDB_POINT_H

#include <string>
#include <vector>
#include <cstdint>

namespace ems {
namespace net {

enum class YKStatus {
    Idle = 0,
    Pending = 1,
    Executing = 2,
    Success = 3,
    Failed = 4
};

enum class PointQuality {
    Good = 0,
    Invalid = 1,
    Reserved = 2,
    Error = 3
};

struct YCPoint {
    double value{0.0};
    uint64_t timestamp{0};
    int quality{0};
    int status{0};
};

struct YXPoint {
    int value{0};
    uint64_t timestamp{0};
    int quality{0};
    int status{0};
};

struct YKPoint {
    int value{0};
    int command_type{0};
    uint64_t timestamp{0};
    YKStatus status{YKStatus::Idle};
    int result{0};
};

struct YTPoint {
    double value{0.0};
    uint64_t timestamp{0};
    int quality{0};
    int status{0};
};

}
}

#endif
