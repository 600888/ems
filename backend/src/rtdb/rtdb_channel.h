#pragma once

/**
 * @file rtdb_channel.h
 * @brief 通道配置与协议类型 - 引用共享模型定义
 *
 * 协议类型 (ProtocolType) 已统一迁移至 inc/models/base.h。
 * ChannelConfig 已迁移至公共头文件 rtdb_config.h。
 */

#include <optional>
#include <string>

#include "models/base.h"
#include "rtdb_config.h"

namespace ems {
namespace rtdb {

// ============================================================
// 协议类型 <-> 字符串 转换函数
// ============================================================

/**
 * @brief 协议类型转字符串
 */
const char* protocolTypeToString(model::ProtocolType t);

/**
 * @brief 字符串转协议类型
 * @return 成功返回协议类型，失败返回 nullopt
 */
std::optional<model::ProtocolType> stringToProtocolType(const std::string& s);

}  // namespace rtdb
}  // namespace ems
