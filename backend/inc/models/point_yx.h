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

class PointYx : public BasePoint {
public:
    PointYx() = default;
    explicit PointYx(int64_t id) : BasePoint(id) {}

    int64_t channel_id{0};

    std::string decode_code;
    int bit{0};
    bool reverse{false};

    int iec_common_address{0};
    int iec_cot{3};
    int iec_quality{0};

    std::string description;

    json to_json() const override {
        json j = BasePoint::to_json();
        j["channel_id"] = channel_id;
        j["point_type"] = "YX";
        j["decode_code"] = decode_code;
        j["bit"] = bit;
        j["reverse"] = reverse;
        j["iec_common_address"] = iec_common_address;
        j["iec_cot"] = iec_cot;
        j["iec_quality"] = iec_quality;
        j["description"] = description;
        return j;
    }

    void from_json(const json& j) override {
        BasePoint::from_json(j);

        if (j.contains("channel_id")) channel_id = j["channel_id"].get<int64_t>();
        if (j.contains("decode_code")) decode_code = j["decode_code"].get<std::string>();
        if (j.contains("bit")) bit = j["bit"].get<int>();
        if (j.contains("reverse")) reverse = j["reverse"].get<bool>();
        if (j.contains("iec_common_address")) iec_common_address = j["iec_common_address"].get<int>();
        if (j.contains("iec_cot")) iec_cot = j["iec_cot"].get<int>();
        if (j.contains("iec_quality")) iec_quality = j["iec_quality"].get<int>();
        if (j.contains("description")) description = j["description"].get<std::string>();
    }

    bool transform_value(bool raw_value) const {
        return reverse ? !raw_value : raw_value;
    }
};

using PointYXList = std::vector<std::shared_ptr<PointYx>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_YX_H
