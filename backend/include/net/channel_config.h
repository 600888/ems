#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ems::model {
/** @brief 采集插件协议类型，不属于实时库的持久数据模型。 */
enum class ProtocolType : int {
    ModbusRTU = 0,
    ModbusTCP = 1,
    IEC104 = 2,
    DLT645 = 3,
    IEC61850 = 4,
    OPCUA = 5,
    MQTT = 6,
    CAN = 7,
    DLT698 = 8,
    Other = 99
};
}  // namespace ems::model

namespace ems::net {
/** @brief 通信通道运行配置；与实时库后端无关。 */
struct ChannelConfig {
    std::string channel_id;
    std::map<std::int64_t, std::string> logical_channel_ids;
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

/**
 * @brief 返回插件协议名称。
 * @param[in] t 协议类型。
 * @return 静态协议名称，不转移所有权。
 */
inline const char* protocolTypeToString(model::ProtocolType t) {
    switch (t) {
        case model::ProtocolType::ModbusRTU:
            return "ModbusRTU";
        case model::ProtocolType::ModbusTCP:
            return "ModbusTCP";
        case model::ProtocolType::IEC104:
            return "IEC104";
        case model::ProtocolType::DLT645:
            return "DLT645";
        case model::ProtocolType::DLT698:
            return "DLT698";
        case model::ProtocolType::IEC61850:
            return "IEC61850";
        case model::ProtocolType::OPCUA:
            return "OPCUA";
        case model::ProtocolType::MQTT:
            return "MQTT";
        case model::ProtocolType::CAN:
            return "CAN";
        case model::ProtocolType::Other:
            return "Other";
    }
    return "Unknown";
}

/**
 * @brief 解析插件协议名称。
 * @param[in] s 协议名称。
 * @return 协议类型；未知名称为空。
 */
inline std::optional<model::ProtocolType> stringToProtocolType(const std::string& s) {
    if (s == "ModbusRTU") return model::ProtocolType::ModbusRTU;
    if (s == "ModbusTCP") return model::ProtocolType::ModbusTCP;
    if (s == "IEC104") return model::ProtocolType::IEC104;
    if (s == "DLT645") return model::ProtocolType::DLT645;
    if (s == "DLT698") return model::ProtocolType::DLT698;
    if (s == "IEC61850") return model::ProtocolType::IEC61850;
    if (s == "OPCUA") return model::ProtocolType::OPCUA;
    if (s == "MQTT") return model::ProtocolType::MQTT;
    if (s == "CAN") return model::ProtocolType::CAN;
    return std::nullopt;
}

}  // namespace ems::net
