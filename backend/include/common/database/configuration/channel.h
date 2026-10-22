/**
 * @file channel.h
 * @brief 通道表 配置记录定义
 */
#pragma once

#include <cstdint>
#include <string>

namespace ems {
namespace database {

struct ChannelRecord {
    int64_t id = 0;
    std::string code;       // 通道编码
    std::string name;       // 通道名称
    int64_t device_id = 0;  // 关联设备ID
    int protocol_type = 1;  // 协议类型
    int conn_type = 1;      // 连接类型

    // TCP 参数
    std::string ip;
    int port = 502;

    // 串口参数
    std::string com_port;
    int baud_rate = 9600;
    int data_bits = 8;
    int stop_bits = 1;
    std::string parity = "N";

    // Modbus 参数
    std::string rtu_addr = "1";
    int timeout = 5;

    // 元数据
    int enable = 1;
    std::string created_at;
    std::string updated_at;
};

}  // namespace database
}  // namespace ems
