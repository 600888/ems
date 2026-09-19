/**
 * @file channel.h
 * @brief 通道模型（design.md 第 6.1 节）
 *
 * ChannelConfig 是通道身份与版本化定义，不持有运行 session；
 * 连接生命周期归采集模块。purpose/role 与连接方式独立。
 */
#pragma once

#include <string>

#include <common/core/identity.h>

namespace ems::model {

/// 连接方式
enum class ConnectionMode : std::uint8_t {
    Serial    = 0,  ///< 串行（RS485/RS232）
    TcpClient = 1,  ///< TCP 客户端（主动连接设备）
    TcpServer = 2,  ///< TCP 服务端（等待设备接入）
    Udp       = 3
};

/// 通道用途
enum class ChannelPurpose : std::uint8_t {
    Telemetry  = 0,  ///< 仅采集
    Control    = 1,  ///< 仅控制
    Both       = 2,  ///< 采集 + 控制
    Forwarding = 3   ///< 北向转发
};

/// 协议 profile 引用（版本化；协议实现在协议模块）
struct ProtocolProfileRef {
    std::string protocol;         ///< 协议名，如 modbus_rtu / iec104 / iec61850
    std::string profile_version;  ///< profile 版本
};

/// 通道配置（身份 + 版本化定义）
struct ChannelConfig {
    core::ChannelId channel_id;
    core::StationId station_id;    ///< 所属站
    std::string code;              ///< 业务编码
    std::string name;
    ChannelPurpose purpose{ChannelPurpose::Telemetry};
    std::string role;              ///< 协议角色（如 master/slave），由 profile 约束
    ConnectionMode connection_mode{ConnectionMode::TcpClient};
    ProtocolProfileRef profile;
    std::string connection_resource;  ///< 连接资源引用（串口名/host:port 等），运行连接归采集
    bool enabled{true};
};

inline const char* to_string(ConnectionMode m) noexcept {
    switch (m) {
        case ConnectionMode::Serial:    return "Serial";
        case ConnectionMode::TcpClient: return "TcpClient";
        case ConnectionMode::TcpServer: return "TcpServer";
        case ConnectionMode::Udp:       return "Udp";
    }
    return "Unknown";
}

}  // namespace ems::model
