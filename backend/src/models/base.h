/**
 * @file base.h
 * @brief EMS数据模型基类定义
 * @author cdy
 * @date 2026-04-01
 */
#ifndef EMS_MODEL_BASE_H
#define EMS_MODEL_BASE_H

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "net/protocol/modbus/modbus_types.h"

namespace ems {
namespace model {

using json = nlohmann::json;
using TimePoint = std::chrono::system_clock::time_point;

/** @brief 提供 JSON 编解码的模型基类。 */
class BaseModel {
   public:
    /** @brief 销毁模型实例。 */
    virtual ~BaseModel() = default;

    /**
     * @brief 将模型编码为 JSON 对象。
     * @return 序列化后的 JSON 数据。
     */
    virtual json to_json() const = 0;

    /**
     * @brief 从 JSON 对象更新模型字段。
     * @param[in] j 元素索引。
     */
    virtual void from_json(const json& j) = 0;

    /**
     * @brief 返回便于诊断的缩进 JSON 文本。
     * @return 对应的文本内容。
     */
    virtual std::string to_string() const { return to_json().dump(2); }
};

/** @brief 带数据库 ID 的模型基类。 */
class BaseModelWithId : public BaseModel {
   public:
    int64_t id{0};

    /** @brief 构造 ID 为零的模型。 */
    BaseModelWithId() = default;

    /**
     * @brief 从数据库 ID 构造模型。
     * @param[in] id 对象标识。
     */
    explicit BaseModelWithId(int64_t id) : id(id) {}

    /**
     * @copydoc BaseModel::to_json
     * @return 序列化后的 JSON 数据。
     */
    virtual json to_json() const override {
        json j;
        j["id"] = id;
        return j;
    }

    /**
     * @copydoc BaseModel::from_json
     * @param[in] j 元素索引。
     */
    void from_json(const json& j) override {
        if (j.contains("id")) {
            id = j["id"].get<int64_t>();
        }
    }
};

/** @brief 为模型提供创建和更新时间的 JSON 转换。 */
struct TimestampMixin {
    TimePoint created_at{std::chrono::system_clock::now()};
    TimePoint updated_at{std::chrono::system_clock::now()};

    /**
     * @brief 将时间戳编码为 Unix 秒。
     * @return 序列化后的 JSON 数据。
     */
    json to_json() const {
        return {{"created_at",
                 std::chrono::duration_cast<std::chrono::seconds>(created_at.time_since_epoch())
                     .count()},
                {"updated_at",
                 std::chrono::duration_cast<std::chrono::seconds>(updated_at.time_since_epoch())
                     .count()}};
    }

    /**
     * @brief 从 Unix 秒字段恢复时间戳。
     * @param[in] j 元素索引。
     */
    void from_json(const json& j) {
        if (j.contains("created_at")) {
            created_at = TimePoint(std::chrono::seconds(j["created_at"].get<int64_t>()));
        }
        if (j.contains("updated_at")) {
            updated_at = TimePoint(std::chrono::seconds(j["updated_at"].get<int64_t>()));
        }
    }
};

/** @brief 为模型提供状态与启用标记的 JSON 转换。 */
struct StatusMixin {
    int status{0};
    bool enable{true};

    /**
     * @brief 将状态与启用标记编码为 JSON。
     * @return 序列化后的 JSON 数据。
     */
    json to_json() const { return {{"status", status}, {"enable", enable}}; }

    /**
     * @brief 从 JSON 更新状态与启用标记。
     * @param[in] j 元素索引。
     */
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
    DLT698 = 8,
    Other = 99
};

enum class ConnectionType : int { Serial = 0, TCP = 1, TCPServer = 2, UDP = 3 };

enum class DeviceType : int {
    // 通用设备类型
    RTU = 0,
    Protection = 1,
    Meter = 2,
    IED = 3,
    PLC = 4,
    Virtual = 5,
    Gateway = 6,
    // 储能/能源专用设备类型
    UNKNOWN = 10,
    INVERTER = 11,
    BATTERY = 12,
    BMS = 13,
    TRANSFORMER = 14,
    HVAC = 15,
    FIRE_CONTROL = 16,
    ENV_MONITOR = 17,
    Other = 99
};

enum class PointType : int { YC = 0, YX = 1, YK = 2, YT = 3 };

enum class YKCommandType : int { Pulse = 0, Latch = 1, Step = 2 };

/** @brief 存量模型沿用 Modbus 解码与端序类型，保持调用接口兼容。 */
using DecodeCode = net::modbus::DecodeCode;
using EndianCode = net::modbus::EndianCode;

/**
 * @brief 获取单寄存器类型的对应字节数
 * @param[in] code 对象编码。
 * @return 处理或统计得到的数据项数量。
 */
constexpr int decode_size(DecodeCode code) noexcept {
    switch (code) {
        case DecodeCode::UINT8:
        case DecodeCode::INT8:
        case DecodeCode::BOOL:
        case DecodeCode::BIT:
            return 1;
        case DecodeCode::UINT16:
        case DecodeCode::INT16:
            return 2;
        case DecodeCode::UINT32:
        case DecodeCode::INT32:
        case DecodeCode::FLOAT:
            return 4;
        case DecodeCode::DOUBLE:
            return 8;
    }
    return 2;
}

/**
 * @brief 获取解码类型对应的寄存器数量
 * @param[in] code 对象编码。
 * @return 处理或统计得到的数据项数量。
 */
constexpr int decode_reg_count(DecodeCode code) noexcept { return decode_size(code) / 2; }

/**
 * @brief 测点品质枚举
 */
enum class PointQuality : int { Good = 0, Invalid = 1, Reserved = 2, Error = 3 };

/**
 * @brief 遥控执行状态枚举
 */
enum class YKStatus : int { Idle = 0, Pending = 1, Executing = 2, Success = 3, Failed = 4 };

/** @brief 带采集地址和解码元数据的测点模型。 */
class BasePoint : public BaseModelWithId {
   public:
    /** @brief 构造默认测点模型。 */
    BasePoint() = default;

    /**
     * @brief 从数据库 ID 构造测点模型。
     * @param[in] id 对象标识。
     */
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

    /**
     * @copydoc BaseModel::to_json
     * @return 序列化后的 JSON 数据。
     */
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

    /**
     * @copydoc BaseModel::from_json
     * @param[in] j 元素索引。
     */
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
        if (j.contains("point_type"))
            point_type = static_cast<PointType>(j["point_type"].get<int>());
        if (j.contains("status")) status = j["status"].get<int>();
        if (j.contains("enable")) enable = j["enable"].get<bool>();
        if (j.contains("timestamp")) timestamp = j["timestamp"].get<uint64_t>();
        if (j.contains("is_signed")) is_signed = j["is_signed"].get<bool>();
        if (j.contains("is_valid") && !j["is_valid"].is_null()) {
            is_valid = j["is_valid"].get<bool>();
        }
        if (j.contains("is_locked_by_mapping"))
            is_locked_by_mapping = j["is_locked_by_mapping"].get<bool>();
    }

   private:
    /**
     * @brief 将数值格式化为固定宽度十六进制文本。
     * @param[in] value 输入值。
     * @return 对应的文本内容。
     */
    static std::string decimal_to_hex_formatted(int value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%08X", value);
        return std::string(buf);
    }
};

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_BASE_H
