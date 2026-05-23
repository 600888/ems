#pragma once

/**
 * @file rtdb_channel.h
 * @brief 通道配置与协议类型 - 引用共享模型定义
 *
 * 协议类型 (ProtocolType) 已统一迁移至 inc/models/base.h。
 * ChannelConfig 保持本层定义，但集成共享模型的增量。
 */

#include <string>
#include <cstdint>
#include <optional>
#include "models/base.h"

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

/**
 * @brief 通道配置
 *
 * 定义物理/逻辑通道的连接参数。
 */
struct ChannelConfig {
    int64_t      id_{0};             // 通道 ID
    std::string  name_;              // 通道名称
    model::ProtocolType protocol_{model::ProtocolType::ModbusRTU}; // 协议类型
    std::string  connection_;        // 连接串 (如 "/dev/ttyS0", "192.168.1.1:502")
    std::string  settings_;          // JSON 格式的扩展参数
    int          baudRate_{9600};    // 波特率
    int          dataBits_{8};       // 数据位
    int          stopBits_{1};       // 停止位
    std::string  parity_{"none"};    // 校验位
    int          status_{1};         // 使能状态
};

}  // namespace rtdb
}  // namespace ems
