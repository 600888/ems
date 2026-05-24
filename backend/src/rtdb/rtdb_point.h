#ifndef EMS_RTDB_POINT_H
#define EMS_RTDB_POINT_H

/**
 * @file rtdb_point.h
 * @brief 运行时测点类型 - 引用共享模型定义 (point_runtime.hpp)
 *
 * 运行时测点数据结构已统一迁移至 inc/models/point_runtime.hpp。
 * 本文件仅作为兼容层，将共享模型类型引入 ems::rtdb 命名空间，
 * 同时保留 PointConfig 配置转换结构体。
 *
 * ChannelConfig 已在公共头文件 rtdb_config.h 中统一定义。
 */

#include "models/point_runtime.hpp"
#include "rtdb_config.h"

#include <string>
#include <cstdint>

namespace ems {
namespace rtdb {

// ============================================================
// 运行时测点类型别名（引用共享模型）
// ============================================================

using BasePoint     = model::IRuntimePoint;
using YcPoint       = model::YcPoint;
using YxPoint       = model::YxPoint;
using YkPoint       = model::YkPoint;
using YtPoint       = model::YtPoint;

// 枚举类型别名（保持 rtdb 层兼容）
using DecodeCode    = model::DecodeCode;
using PointType     = model::PointType;
using PointQuality  = model::PointQuality;
using YKStatus      = model::YKStatus;
using YKCommandType = model::YKCommandType;

// ============================================================
// 配置转换结构体（本层专用）
// ============================================================

/**
 * @brief 测点配置（用于 DB <-> 运行时配置 转换）
 */
struct PointConfig {
    std::string code_;
    std::string name_;
    model::PointType pointType_{model::PointType::YC};
    int64_t channelId_{0};
    int64_t deviceId_{0};
    model::DecodeCode decodeCode_{model::DecodeCode::UINT16};

    double mulCoe_{1.0};
    double addCoe_{0.0};
    double minLimit_{-9999999.0};
    double maxLimit_{9999999.0};
    std::string unit_;

    int bit_{0};
    bool reverse_{false};
    int commandType_{0};

    std::string description_;
    int status_{1};
    std::string createdAt_;
    std::string updatedAt_;
};

} // namespace rtdb
} // namespace ems

#endif // EMS_RTDB_POINT_H
