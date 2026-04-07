/**
 * @file base.h
 * @brief EMS数据模型基类定义
 * @author cdy
 * @date 2026-04-01
 */
#ifndef EMS_MODEL_BASE_H
#define EMS_MODEL_BASE_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <variant>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace ems {
namespace model {

using json = nlohmann::json;
using TimePoint = std::chrono::system_clock::time_point;

class BaseModel {
public:
    virtual ~BaseModel() = default;
    
    virtual json to_json() const = 0;
    virtual void from_json(const json& j) = 0;
    
    virtual std::string to_string() const {
        return to_json().dump(2);
    }
};

class BaseModelWithId : public BaseModel {
public:
    int64_t id{0};
    BaseModelWithId() = default;
    explicit BaseModelWithId(int64_t id) : id(id) {}
    
    virtual json to_json() const override {
        json j;
        j["id"] = id;
        return j;
    }
    
    void from_json(const json& j) override {
        if (j.contains("id")) {
            id = j["id"].get<int64_t>();
        }
    }
};

struct TimestampMixin {
    TimePoint created_at{std::chrono::system_clock::now()};
    TimePoint updated_at{std::chrono::system_clock::now()};
    
    json to_json() const {
        return {
            {"created_at", std::chrono::duration_cast<std::chrono::seconds>(
                created_at.time_since_epoch()).count()},
            {"updated_at", std::chrono::duration_cast<std::chrono::seconds>(
                updated_at.time_since_epoch()).count()}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("created_at")) {
            created_at = TimePoint(std::chrono::seconds(j["created_at"].get<int64_t>()));
        }
        if (j.contains("updated_at")) {
            updated_at = TimePoint(std::chrono::seconds(j["updated_at"].get<int64_t>()));
        }
    }
};

struct StatusMixin {
    int status{0};
    bool enable{true};
    
    json to_json() const {
        return {
            {"status", status},
            {"enable", enable}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("status")) status = j["status"].get<int>();
        if (j.contains("enable")) enable = j["enable"].get<bool>();
    }
};

enum class DataQuality : int {
    Good = 0,
    Invalid = 1,
    Uncertain = 2,
    Overflow = 3,
    OutOfRange = 4,
    CommError = 5,
    DeviceError = 6,
    SensorError = 7
};

enum class ProtocolType : int {
    ModbusRTU = 0,
    ModbusTCP = 1,
    IEC104 = 2,
    DLT645 = 3,
    IEC61850 = 4,
    OPCUA = 5,
    MQTT = 6,
    CAN = 7,
    Other = 99
};

enum class ConnectionType : int {
    Serial = 0,
    TCP = 1,
    TCPServer = 2,
    UDP = 3
};

enum class DeviceType : int {
    RTU = 0,
    Protection = 1,
    Meter = 2,
    IED = 3,
    PLC = 4,
    Virtual = 5,
    Gateway = 6,
    Other = 99
};

enum class PointType : int {
    YC = 0,
    YX = 1,
    YK = 2,
    YT = 3
};

enum class YKCommandType : int {
    Pulse = 0,
    Latch = 1,
    Step = 2
};

} // namespace model
} // namespace ems

#endif // EMS_MODEL_BASE_H
