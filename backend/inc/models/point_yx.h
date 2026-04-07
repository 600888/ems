/**
 * @file point_yx.h
 * @brief 遥信点模型定义（开关量状态）
 */
#ifndef EMS_MODEL_POINT_YX_H
#define EMS_MODEL_POINT_YX_H

#include <string>
#include <vector>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

class PointYX : public BaseModelWithId, public TimestampMixin, public StatusMixin {
public:
    PointYX() = default;
    explicit PointYX(int id) : BaseModelWithId(id) {}
    
    std::string code;
    std::string name;
    int channel_id{0};
    
    int rtu_addr{1};
    std::string reg_addr;
    int func_code{1};
    std::string decode_code;
    int bit{0};
    bool reverse{false};
    
    int iec_common_address{0};
    int iec_cot{3};
    int iec_quality{0};
    
    std::string description;
    
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["code"] = code;
        j["name"] = name;
        j["channel_id"] = channel_id;
        j["point_type"] = "YX";
        j["rtu_addr"] = rtu_addr;
        j["reg_addr"] = reg_addr;
        j["func_code"] = func_code;
        j["decode_code"] = decode_code;
        j["bit"] = bit;
        j["reverse"] = reverse;
        j["iec_common_address"] = iec_common_address;
        j["iec_cot"] = iec_cot;
        j["iec_quality"] = iec_quality;
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
        if (j.contains("bit")) bit = j["bit"].get<int>();
        if (j.contains("reverse")) reverse = j["reverse"].get<bool>();
        if (j.contains("iec_common_address")) iec_common_address = j["iec_common_address"].get<int>();
        if (j.contains("iec_cot")) iec_cot = j["iec_cot"].get<int>();
        if (j.contains("iec_quality")) iec_quality = j["iec_quality"].get<int>();
        if (j.contains("description")) description = j["description"].get<std::string>();
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }
    
    bool transform_value(bool raw_value) const {
        return reverse ? !raw_value : raw_value;
    }
};

using PointYXList = std::vector<std::shared_ptr<PointYX>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_YX_H
