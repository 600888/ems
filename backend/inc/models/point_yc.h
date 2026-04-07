/**
 * @file point_yc.h
 * @brief 遥测点模型定义（模拟量测量）
 */
#ifndef EMS_MODEL_POINT_YC_H
#define EMS_MODEL_POINT_YC_H

#include <string>
#include <vector>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

class PointYC : public BaseModelWithId, public TimestampMixin, public StatusMixin {
public:
    PointYC() = default;
    explicit PointYC(int64_t id) : BaseModelWithId(id) {}
    
    std::string code;
    std::string name;
    int64_t channel_id{0};
    
    int rtu_addr{1};
    std::string reg_addr;
    int func_code{3};
    std::string decode_code;
    
    double mul_coe{1.0};
    double add_coe{0.0};
    double max_limit{9999999.0};
    double min_limit{-9999999.0};
    
    int iec_common_address{0};
    int iec_cot{3};
    int iec_quality{0};
    
    std::string unit;
    std::string description;
    
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["code"] = code;
        j["name"] = name;
        j["channel_id"] = channel_id;
        j["point_type"] = "YC";
        j["rtu_addr"] = rtu_addr;
        j["reg_addr"] = reg_addr;
        j["func_code"] = func_code;
        j["decode_code"] = decode_code;
        j["mul_coe"] = mul_coe;
        j["add_coe"] = add_coe;
        j["max_limit"] = max_limit;
        j["min_limit"] = min_limit;
        j["iec_common_address"] = iec_common_address;
        j["iec_cot"] = iec_cot;
        j["iec_quality"] = iec_quality;
        j["unit"] = unit;
        j["description"] = description;
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();
        return j;
    }
    
    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);
        
        if (j.contains("code")) code = j["code"].get<std::string>();
        if (j.contains("name")) name = j["name"].get<std::string>();
        if (j.contains("channel_id")) channel_id = j["channel_id"].get<int64_t>();
        if (j.contains("rtu_addr")) rtu_addr = j["rtu_addr"].get<int>();
        if (j.contains("reg_addr")) reg_addr = j["reg_addr"].get<std::string>();
        if (j.contains("func_code")) func_code = j["func_code"].get<int>();
        if (j.contains("decode_code")) decode_code = j["decode_code"].get<std::string>();
        if (j.contains("mul_coe")) mul_coe = j["mul_coe"].get<double>();
        if (j.contains("add_coe")) add_coe = j["add_coe"].get<double>();
        if (j.contains("max_limit")) max_limit = j["max_limit"].get<double>();
        if (j.contains("min_limit")) min_limit = j["min_limit"].get<double>();
        if (j.contains("iec_common_address")) iec_common_address = j["iec_common_address"].get<int>();
        if (j.contains("iec_cot")) iec_cot = j["iec_cot"].get<int>();
        if (j.contains("iec_quality")) iec_quality = j["iec_quality"].get<int>();
        if (j.contains("unit")) unit = j["unit"].get<std::string>();
        if (j.contains("description")) description = j["description"].get<std::string>();
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }
    
    double transform_value(double raw_value) const {
        return raw_value * mul_coe + add_coe;
    }
    
    bool is_value_valid(double value) const {
        return value >= min_limit && value <= max_limit;
    }
};

using PointYCList = std::vector<std::shared_ptr<PointYC>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_YC_H
