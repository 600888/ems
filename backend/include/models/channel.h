/**
 * @file channel.h
 * @brief 通道模型定义
 */
#ifndef EMS_MODEL_CHANNEL_H
#define EMS_MODEL_CHANNEL_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base.h"

namespace ems {
namespace model {

class channel : public BaseModelWithId, public TimestampMixin, public StatusMixin {
   public:
    channel() = default;
    explicit channel(int64_t id) : BaseModelWithId(id) {}

    std::string code;
    std::string name;
    std::optional<int64_t> device_id;

    ProtocolType protocol_type{ProtocolType::ModbusTCP};
    ConnectionType conn_type{ConnectionType::TCP};

    std::optional<std::string> ip;
    std::optional<int> port;

    std::optional<std::string> com_port;
    std::optional<int> baud_rate{9600};
    std::optional<int> data_bits{8};
    std::optional<int> stop_bits{1};
    std::optional<char> parity{'N'};

    std::string rtu_addr{"1"};
    int timeout{5};

    std::vector<std::shared_ptr<class point_yc>> yc_points;
    std::vector<std::shared_ptr<class point_yx>> yx_points;
    std::vector<std::shared_ptr<class point_yk>> yk_points;
    std::vector<std::shared_ptr<class point_yt>> yt_points;
    std::vector<std::shared_ptr<class Slave>> slaves;

    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["code"] = code;
        j["name"] = name;
        j["device_id"] = device_id.has_value() ? json(device_id.value()) : json(nullptr);
        j["protocol_type"] = static_cast<int>(protocol_type);
        j["protocol_name"] = get_protocol_name();
        j["conn_type"] = static_cast<int>(conn_type);
        j["conn_type_name"] = get_conn_type_name();

        j["ip"] = ip.has_value() ? json(ip.value()) : json(nullptr);
        j["port"] = port.has_value() ? json(port.value()) : json(nullptr);
        j["com_port"] = com_port.has_value() ? json(com_port.value()) : json(nullptr);
        j["baud_rate"] = baud_rate.has_value() ? json(baud_rate.value()) : json(nullptr);
        j["data_bits"] = data_bits.has_value() ? json(data_bits.value()) : json(nullptr);
        j["stop_bits"] = stop_bits.has_value() ? json(stop_bits.value()) : json(nullptr);
        j["parity"] = parity.has_value() ? json(parity.value()) : json(nullptr);

        j["rtu_addr"] = rtu_addr;
        j["timeout"] = timeout;
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();

        j["point_counts"] = {{"yc", yc_points.size()},
                             {"yx", yx_points.size()},
                             {"yk", yk_points.size()},
                             {"yt", yt_points.size()}};

        return j;
    }

    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);

        if (j.contains("code")) code = j["code"].get<std::string>();
        if (j.contains("name")) name = j["name"].get<std::string>();
        if (j.contains("device_id") && !j["device_id"].is_null()) {
            device_id = j["device_id"].get<int64_t>();
        }
        if (j.contains("protocol_type")) {
            protocol_type = static_cast<ProtocolType>(j["protocol_type"].get<int>());
        }
        if (j.contains("conn_type")) {
            conn_type = static_cast<ConnectionType>(j["conn_type"].get<int>());
        }

        if (j.contains("ip") && !j["ip"].is_null()) ip = j["ip"].get<std::string>();
        if (j.contains("port") && !j["port"].is_null()) port = j["port"].get<int>();
        if (j.contains("com_port") && !j["com_port"].is_null())
            com_port = j["com_port"].get<std::string>();
        if (j.contains("baud_rate") && !j["baud_rate"].is_null())
            baud_rate = j["baud_rate"].get<int>();
        if (j.contains("data_bits") && !j["data_bits"].is_null())
            data_bits = j["data_bits"].get<int>();
        if (j.contains("stop_bits") && !j["stop_bits"].is_null())
            stop_bits = j["stop_bits"].get<int>();
        if (j.contains("parity") && !j["parity"].is_null()) parity = j["parity"].get<char>();

        if (j.contains("rtu_addr")) rtu_addr = j["rtu_addr"].get<std::string>();
        if (j.contains("timeout")) timeout = j["timeout"].get<int>();
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }

    std::string get_protocol_name() const {
        switch (protocol_type) {
            case ProtocolType::ModbusRTU:
                return "ModbusRTU";
            case ProtocolType::ModbusTCP:
                return "ModbusTCP";
            case ProtocolType::IEC104:
                return "IEC104";
            case ProtocolType::DLT645:
                return "DLT645";
            case ProtocolType::IEC61850:
                return "IEC61850";
            case ProtocolType::OPCUA:
                return "OPCUA";
            case ProtocolType::MQTT:
                return "MQTT";
            default:
                return "Unknown";
        }
    }

    std::string get_conn_type_name() const {
        switch (conn_type) {
            case ConnectionType::Serial:
                return "Serial";
            case ConnectionType::TCP:
                return "TCP";
            case ConnectionType::TCPServer:
                return "TCPServer";
            case ConnectionType::UDP:
                return "UDP";
            default:
                return "Unknown";
        }
    }

    int get_total_point_count() const {
        return static_cast<int>(yc_points.size() + yx_points.size() + yk_points.size() +
                                yt_points.size());
    }
};

using ChannelList = std::vector<std::shared_ptr<channel>>;

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_CHANNEL_H
