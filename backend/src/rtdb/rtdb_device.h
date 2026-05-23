#pragma once

/**
 * @file rtdb_device.h
 * @brief 设备配置 - 引用共享模型定义
 *
 * 设备类型 (DeviceType) 已统一迁移至 inc/models/base.h。
 * DeviceConfig 保持本层定义，但集成共享模型的增量。
 */

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "models/base.h"

namespace ems {
namespace rtdb {

// ============================================================
// 设备类型 <-> 字符串 转换函数
// ============================================================

/**
 * @brief 设备类型转字符串
 */
const char* deviceTypeToString(model::DeviceType t);

/**
 * @brief 字符串转设备类型
 * @return 成功返回设备类型，失败返回 nullopt
 */
std::optional<model::DeviceType> stringToDeviceType(const std::string& s);

/**
 * @brief 设备配置
 *
 * 定义设备信息，关联所属通道和设备下的测点集合。
 * 一个设备可以绑定多个通道（如双路采集），
 * 一个设备也可以绑定多个测点（YC/YX/YK/YT）。
 */
struct DeviceConfig {
    int64_t      id_{0};                           // 设备 ID
    std::string  name_;                            // 设备名称
    model::DeviceType deviceType_{model::DeviceType::UNKNOWN}; // 设备类型

    // ---- 通道关联 ----
    std::vector<int64_t> channelIds_;

    // ---- 测点关联 ----
    std::vector<std::string> pointCodes_;

    // ---- 元数据 ----
    std::string  description_;                     // 设备描述
    std::string  location_;                        // 安装位置
    int          status_{1};                       // 使能状态
    std::string  createdAt_;                       // 创建时间
    std::string  updatedAt_;                       // 更新时间
};

}  // namespace rtdb
}  // namespace ems
