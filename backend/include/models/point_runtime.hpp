/**
 * @file point_runtime.hpp
 * @brief 运行时测点数据结构定义（实时数据值、品质、时间戳等）
 *
 * 与配置测点模型 (point_yc.h/point_yx.h/point_yk.h/point_yt.h) 不同，
 * 本文件定义的是运行时实时数据载体，用于 Redis/内存 中的测点值存取。
 * 使用 nlohmann::json 进行序列化/反序列化。
 */
#ifndef EMS_MODEL_POINT_RUNTIME_HPP
#define EMS_MODEL_POINT_RUNTIME_HPP

#include <string>
#include <cstdint>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

// ============================================================
// 运行时测点虚基类接口
// ============================================================

/**
 * @brief 运行时测点基类接口
 *
 * 所有运行时测点 (YC/YX/YK/YT) 均继承此类，
 * 提供统一的访问、序列化、更新接口。
 * 使用 nlohmann::json 进行序列化/反序列化。
 */
class IRuntimePoint {
public:
    virtual ~IRuntimePoint() = default;

    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    virtual PointType type() const = 0;
    virtual uint64_t timestamp() const = 0;
    virtual PointQuality quality() const = 0;

    virtual void update(const IRuntimePoint& other) = 0;
    virtual json to_json() const = 0;
    virtual void from_json(const json& j) = 0;

    std::string to_string() const {
        return to_json().dump();
    }

    void from_string(const std::string& s) {
        from_json(json::parse(s));
    }
};

// ============================================================
// YcPoint - 遥测运行时数据
// ============================================================

struct YcPoint : public IRuntimePoint {
    std::string  code_;
    std::string  name_;
    DecodeCode   decodeCode_{DecodeCode::UINT16};
    double       value_{0.0};
    double       rawValue_{0.0};
    double       mulCoe_{1.0};
    double       addCoe_{0.0};
    double       minLimit_{-9999999.0};
    double       maxLimit_{9999999.0};
    std::string  unit_;
    uint64_t     timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int          status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YC; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    double transformedValue() const {
        return rawValue_ * mulCoe_ + addCoe_;
    }

    void update(const IRuntimePoint& other) override {
        if (other.type() == PointType::YC) {
            const auto& yc = static_cast<const YcPoint&>(other);
            value_ = yc.value_;
            rawValue_ = yc.rawValue_;
            timestamp_ = yc.timestamp_;
            quality_ = yc.quality_;
            status_ = yc.status_;
        }
    }

    json to_json() const override {
        json j;
        j["code"] = code_;
        j["name"] = name_;
        j["point_type"] = "YC";
        j["decode_code"] = static_cast<int>(decodeCode_);
        j["value"] = value_;
        j["raw_value"] = rawValue_;
        j["mul_coe"] = mulCoe_;
        j["add_coe"] = addCoe_;
        j["min_limit"] = minLimit_;
        j["max_limit"] = maxLimit_;
        j["unit"] = unit_;
        j["timestamp"] = timestamp_;
        j["quality"] = static_cast<int>(quality_);
        j["status"] = status_;
        return j;
    }

    void from_json(const json& j) override {
        if (j.contains("code")) code_ = j["code"].get<std::string>();
        if (j.contains("name")) name_ = j["name"].get<std::string>();
        if (j.contains("decode_code")) decodeCode_ = static_cast<DecodeCode>(j["decode_code"].get<int>());
        if (j.contains("value")) value_ = j["value"].get<double>();
        if (j.contains("raw_value")) rawValue_ = j["raw_value"].get<double>();
        if (j.contains("mul_coe")) mulCoe_ = j["mul_coe"].get<double>();
        if (j.contains("add_coe")) addCoe_ = j["add_coe"].get<double>();
        if (j.contains("min_limit")) minLimit_ = j["min_limit"].get<double>();
        if (j.contains("max_limit")) maxLimit_ = j["max_limit"].get<double>();
        if (j.contains("unit")) unit_ = j["unit"].get<std::string>();
        if (j.contains("timestamp")) timestamp_ = j["timestamp"].get<uint64_t>();
        if (j.contains("quality")) quality_ = static_cast<PointQuality>(j["quality"].get<int>());
        if (j.contains("status")) status_ = j["status"].get<int>();
    }
};

// ============================================================
// YxPoint - 遥信运行时数据
// ============================================================

struct YxPoint : public IRuntimePoint {
    std::string  code_;
    std::string  name_;
    DecodeCode   decodeCode_{DecodeCode::BOOL};
    int          value_{0};
    int          bit_{0};
    bool         reverse_{false};
    uint64_t     timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int          status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YX; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    void update(const IRuntimePoint& other) override {
        if (other.type() == PointType::YX) {
            const auto& yx = static_cast<const YxPoint&>(other);
            value_ = yx.value_;
            timestamp_ = yx.timestamp_;
            quality_ = yx.quality_;
            status_ = yx.status_;
        }
    }

    json to_json() const override {
        json j;
        j["code"] = code_;
        j["name"] = name_;
        j["point_type"] = "YX";
        j["decode_code"] = static_cast<int>(decodeCode_);
        j["value"] = value_;
        j["bit"] = bit_;
        j["reverse"] = reverse_;
        j["timestamp"] = timestamp_;
        j["quality"] = static_cast<int>(quality_);
        j["status"] = status_;
        return j;
    }

    void from_json(const json& j) override {
        if (j.contains("code")) code_ = j["code"].get<std::string>();
        if (j.contains("name")) name_ = j["name"].get<std::string>();
        if (j.contains("decode_code")) decodeCode_ = static_cast<DecodeCode>(j["decode_code"].get<int>());
        if (j.contains("value")) value_ = j["value"].get<int>();
        if (j.contains("bit")) bit_ = j["bit"].get<int>();
        if (j.contains("reverse")) reverse_ = j["reverse"].get<bool>();
        if (j.contains("timestamp")) timestamp_ = j["timestamp"].get<uint64_t>();
        if (j.contains("quality")) quality_ = static_cast<PointQuality>(j["quality"].get<int>());
        if (j.contains("status")) status_ = j["status"].get<int>();
    }
};

// ============================================================
// YkPoint - 遥控运行时数据
// ============================================================

struct YkPoint : public IRuntimePoint {
    std::string  code_;
    std::string  name_;
    DecodeCode   decodeCode_{DecodeCode::BOOL};
    int          value_{0};
    int          commandType_{0};
    YKStatus     status_{YKStatus::Idle};
    uint64_t     timestamp_{0};
    uint64_t     executeTime_{0};
    std::string  result_;

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YK; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override {
        return status_ == YKStatus::Success ? PointQuality::Good : PointQuality::Invalid;
    }

    void update(const IRuntimePoint& other) override {
        if (other.type() == PointType::YK) {
            const auto& yk = static_cast<const YkPoint&>(other);
            value_ = yk.value_;
            commandType_ = yk.commandType_;
            status_ = yk.status_;
            timestamp_ = yk.timestamp_;
            executeTime_ = yk.executeTime_;
            result_ = yk.result_;
        }
    }

    json to_json() const override {
        json j;
        j["code"] = code_;
        j["name"] = name_;
        j["point_type"] = "YK";
        j["decode_code"] = static_cast<int>(decodeCode_);
        j["value"] = value_;
        j["command_type"] = commandType_;
        j["yk_status"] = static_cast<int>(status_);
        j["timestamp"] = timestamp_;
        j["execute_time"] = executeTime_;
        j["result"] = result_;
        return j;
    }

    void from_json(const json& j) override {
        if (j.contains("code")) code_ = j["code"].get<std::string>();
        if (j.contains("name")) name_ = j["name"].get<std::string>();
        if (j.contains("decode_code")) decodeCode_ = static_cast<DecodeCode>(j["decode_code"].get<int>());
        if (j.contains("value")) value_ = j["value"].get<int>();
        if (j.contains("command_type")) commandType_ = j["command_type"].get<int>();
        if (j.contains("yk_status")) status_ = static_cast<YKStatus>(j["yk_status"].get<int>());
        if (j.contains("timestamp")) timestamp_ = j["timestamp"].get<uint64_t>();
        if (j.contains("execute_time")) executeTime_ = j["execute_time"].get<uint64_t>();
        if (j.contains("result")) result_ = j["result"].get<std::string>();
    }
};

// ============================================================
// YtPoint - 遥调运行时数据
// ============================================================

struct YtPoint : public IRuntimePoint {
    std::string  code_;
    std::string  name_;
    DecodeCode   decodeCode_{DecodeCode::UINT16};
    double       value_{0.0};
    double       mulCoe_{1.0};
    double       addCoe_{0.0};
    double       minLimit_{-9999999.0};
    double       maxLimit_{9999999.0};
    std::string  unit_;
    uint64_t     timestamp_{0};
    PointQuality quality_{PointQuality::Good};
    int          status_{0};

    std::string code() const override { return code_; }
    std::string name() const override { return name_; }
    PointType type() const override { return PointType::YT; }
    uint64_t timestamp() const override { return timestamp_; }
    PointQuality quality() const override { return quality_; }

    void update(const IRuntimePoint& other) override {
        if (other.type() == PointType::YT) {
            const auto& yt = static_cast<const YtPoint&>(other);
            value_ = yt.value_;
            timestamp_ = yt.timestamp_;
            quality_ = yt.quality_;
            status_ = yt.status_;
        }
    }

    json to_json() const override {
        json j;
        j["code"] = code_;
        j["name"] = name_;
        j["point_type"] = "YT";
        j["decode_code"] = static_cast<int>(decodeCode_);
        j["value"] = value_;
        j["mul_coe"] = mulCoe_;
        j["add_coe"] = addCoe_;
        j["min_limit"] = minLimit_;
        j["max_limit"] = maxLimit_;
        j["unit"] = unit_;
        j["timestamp"] = timestamp_;
        j["quality"] = static_cast<int>(quality_);
        j["status"] = status_;
        return j;
    }

    void from_json(const json& j) override {
        if (j.contains("code")) code_ = j["code"].get<std::string>();
        if (j.contains("name")) name_ = j["name"].get<std::string>();
        if (j.contains("decode_code")) decodeCode_ = static_cast<DecodeCode>(j["decode_code"].get<int>());
        if (j.contains("value")) value_ = j["value"].get<double>();
        if (j.contains("mul_coe")) mulCoe_ = j["mul_coe"].get<double>();
        if (j.contains("add_coe")) addCoe_ = j["add_coe"].get<double>();
        if (j.contains("min_limit")) minLimit_ = j["min_limit"].get<double>();
        if (j.contains("max_limit")) maxLimit_ = j["max_limit"].get<double>();
        if (j.contains("unit")) unit_ = j["unit"].get<std::string>();
        if (j.contains("timestamp")) timestamp_ = j["timestamp"].get<uint64_t>();
        if (j.contains("quality")) quality_ = static_cast<PointQuality>(j["quality"].get<int>());
        if (j.contains("status")) status_ = j["status"].get<int>();
    }
};

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_RUNTIME_HPP
