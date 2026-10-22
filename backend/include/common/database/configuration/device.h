/**
 * @file device.h
 * @brief 设备表 配置记录定义
 */
#pragma once

#include <cstdint>
#include <string>

namespace ems {
namespace database {

struct DeviceRecord {
    int64_t id = 0;
    std::string code;         // 设备编码
    std::string name;         // 设备名称
    int device_type = 0;      // 0=RTU,1=Protection,2=Meter...
    int64_t group_id = 0;     // 设备组ID
    std::string description;  // 描述
    int enable = 1;           // 是否启用
    std::string created_at;   // 创建时间
    std::string updated_at;   // 更新时间
};

}  // namespace database
}  // namespace ems
