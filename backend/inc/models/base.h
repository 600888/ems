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
    // 通用设备类型
    RTU         = 0,
    Protection  = 1,
    Meter       = 2,
    IED         = 3,
    PLC         = 4,
    Virtual     = 5,
    Gateway     = 6,
    // 储能/能源专用设备类型
    UNKNOWN         = 10,
    INVERTER        = 11,
    BATTERY         = 12,
    BMS             = 13,
    TRANSFORMER     = 14,
    HVAC            = 15,
    FIRE_CONTROL    = 16,
    ENV_MONITOR     = 17,
    Other   = 99
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

/**
 * @brief 解码方式枚举
 */
enum class DecodeCode : int {
    UINT16 = 0,
    INT16  = 1,
    UINT32 = 2,
    INT32  = 3,
    FLOAT  = 4,
    DOUBLE = 5,
    UINT8  = 6,
    INT8   = 7,
    BOOL   = 8,
    BIT    = 9
};

/**
 * @brief 测点品质枚举
 */
enum class PointQuality : int {
    Good     = 0,
    Invalid  = 1,
    Reserved = 2,
    Error    = 3
};

/**
 * @brief 遥控执行状态枚举
 */
enum class YKStatus : int {
    Idle      = 0,
    Pending   = 1,
    Executing = 2,
    Success   = 3,
    Failed    = 4
};

class BasePoint : public BaseModelWithId {
public:
    BasePoint() = default;
    explicit BasePoint(int64_t id) : BaseModelWithId(id) {}
    
    std::string rtu_addr{"1"};
    std::string address{"0x0000"};
    std::string hex_address;
    int func_code{3};
    std::string name;
    std::string code;
    int value{0};
    std::string hex_value;
    int frame_type{0};
    std::string decode{"0x41"};
    bool is_updating{false};
    bool is_simulated{false};
    bool is_plan{false};
    PointType point_type{PointType::YC};
    int status{0};
    bool enable{true};
    uint64_t timestamp{0};
    std::weak_ptr<BasePoint> related_point;
    std::unordered_map<int, int> related_value;
    bool is_signed{false};
    std::optional<bool> is_valid;
    bool is_locked_by_mapping{false};
    
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["rtu_addr"] = rtu_addr;
        j["address"] = address;
        j["hex_address"] = hex_address.empty() ? address : hex_address;
        j["func_code"] = func_code;
        j["name"] = name;
        j["code"] = code;
        j["value"] = value;
        j["hex_value"] = hex_value.empty() ? decimal_to_hex_formatted(value) : hex_value;
        j["frame_type"] = frame_type;
        j["decode"] = decode;
        j["is_updating"] = is_updating;
        j["is_simulated"] = is_simulated;
        j["is_plan"] = is_plan;
        j["point_type"] = static_cast<int>(point_type);
        j["status"] = status;
        j["enable"] = enable;
        j["timestamp"] = timestamp;
        j["is_signed"] = is_signed;
        if (is_valid.has_value()) {
            j["is_valid"] = is_valid.value();
        }
        j["is_locked_by_mapping"] = is_locked_by_mapping;
        return j;
    }
    
    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);
        
        if (j.contains("rtu_addr")) rtu_addr = j["rtu_addr"].get<std::string>();
        if (j.contains("address")) address = j["address"].get<std::string>();
        if (j.contains("hex_address")) hex_address = j["hex_address"].get<std::string>();
        if (j.contains("func_code")) func_code = j["func_code"].get<int>();
        if (j.contains("name")) name = j["name"].get<std::string>();
        if (j.contains("code")) code = j["code"].get<std::string>();
        if (j.contains("value")) value = j["value"].get<int>();
        if (j.contains("hex_value")) hex_value = j["hex_value"].get<std::string>();
        if (j.contains("frame_type")) frame_type = j["frame_type"].get<int>();
        if (j.contains("decode")) decode = j["decode"].get<std::string>();
        if (j.contains("is_updating")) is_updating = j["is_updating"].get<bool>();
        if (j.contains("is_simulated")) is_simulated = j["is_simulated"].get<bool>();
        if (j.contains("is_plan")) is_plan = j["is_plan"].get<bool>();
        if (j.contains("point_type")) point_type = static_cast<PointType>(j["point_type"].get<int>());
        if (j.contains("status")) status = j["status"].get<int>();
        if (j.contains("enable")) enable = j["enable"].get<bool>();
        if (j.contains("timestamp")) timestamp = j["timestamp"].get<uint64_t>();
        if (j.contains("is_signed")) is_signed = j["is_signed"].get<bool>();
        if (j.contains("is_valid") && !j["is_valid"].is_null()) {
            is_valid = j["is_valid"].get<bool>();
        }
        if (j.contains("is_locked_by_mapping")) is_locked_by_mapping = j["is_locked_by_mapping"].get<bool>();
    }
    
private:
    static std::string decimal_to_hex_formatted(int value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%08X", value);
        return std::string(buf);
    }
};

} // namespace model
} // namespace ems

#endif // EMS_MODEL_BASE_H
