/**
 * @file point_yt.h
 * @brief 遥调点模型定义（调节量）
 */
#ifndef EMS_MODEL_POINT_YT_H
#define EMS_MODEL_POINT_YT_H

#include <string>
#include <vector>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

class PointYt : public BasePoint {
public:
    PointYt() = default;
    explicit PointYt(int64_t id) : BasePoint(id) {}

    int64_t channel_id{0};

    std::string decode_code;

    double mul_coe{1.0};
    double add_coe{0.0};
    double max_limit{9999999.0};
    double min_limit{-9999999.0};

    int64_t related_yc_id{0};

    int iec_common_address{0};
    int iec_cot{3};
    int iec_quality{0};

    std::string unit;
    std::string description;

    json to_json() const override {
        json j = BasePoint::to_json();
        j["channel_id"] = channel_id;
        j["point_type"] = "YT";
        j["decode_code"] = decode_code;
        j["mul_coe"] = mul_coe;
        j["add_coe"] = add_coe;
        j["max_limit"] = max_limit;
        j["min_limit"] = min_limit;
        j["related_yc_id"] = related_yc_id;
        j["iec_common_address"] = iec_common_address;
        j["iec_cot"] = iec_cot;
        j["iec_quality"] = iec_quality;
        j["unit"] = unit;
        j["description"] = description;
        return j;
    }

    void from_json(const json& j) override {
        BasePoint::from_json(j);

        if (j.contains("channel_id")) channel_id = j["channel_id"].get<int64_t>();
        if (j.contains("decode_code")) decode_code = j["decode_code"].get<std::string>();
        if (j.contains("mul_coe")) mul_coe = j["mul_coe"].get<double>();
        if (j.contains("add_coe")) add_coe = j["add_coe"].get<double>();
        if (j.contains("max_limit")) max_limit = j["max_limit"].get<double>();
        if (j.contains("min_limit")) min_limit = j["min_limit"].get<double>();
        if (j.contains("related_yc_id")) related_yc_id = j["related_yc_id"].get<int64_t>();
        if (j.contains("iec_common_address")) iec_common_address = j["iec_common_address"].get<int>();
        if (j.contains("iec_cot")) iec_cot = j["iec_cot"].get<int>();
        if (j.contains("iec_quality")) iec_quality = j["iec_quality"].get<int>();
        if (j.contains("unit")) unit = j["unit"].get<std::string>();
        if (j.contains("description")) description = j["description"].get<std::string>();
    }

    double transform_value(double raw_value) const {
        return raw_value * mul_coe + add_coe;
    }

    bool is_value_valid(double value) const {
        return value >= min_limit && value <= max_limit;
    }
};

using PointYTList = std::vector<std::shared_ptr<PointYt>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_YT_H
