/**
 * @file rtdb_config.h
 * @brief RTDB 公共配置类型定义
 *
 * 本文件定义了 ems::rtdb 命名空间下的公共配置结构体，
 * 供 rtdb_channel.h、rtdb_point.h 以及其他模块共享引用，
 * 避免同名类型重复定义导致的编译错误。
 */
#ifndef EMS_RTDB_CONFIG_H
#define EMS_RTDB_CONFIG_H

#include <cstdint>
#include <string>

#include "models/base.h"

namespace ems {
namespace rtdb {

/**
 * @brief 通道配置
 *
 * 定义物理/逻辑通道的连接参数，包括协议类型、网络/串口参数。
 * 由 rtdb_channel.h 和 rtdb_point.h 共享此定义。
 */
struct ChannelConfig {
    int64_t id_{0};                                                 // 通道 ID
    std::string name_;                                              // 通道名称
    model::ProtocolType protocol_{model::ProtocolType::ModbusRTU};  // 协议类型
    std::string connection_;      // 连接串 (如 "/dev/ttyS0", "192.168.1.1:502")
    std::string settings_;        // JSON 格式的扩展参数
    int baudRate_{9600};          // 波特率
    int dataBits_{8};             // 数据位
    int stopBits_{1};             // 停止位
    std::string parity_{"none"};  // 校验位
    int status_{1};               // 使能状态
};

}  // namespace rtdb
}  // namespace ems

#endif  // EMS_RTDB_CONFIG_H
