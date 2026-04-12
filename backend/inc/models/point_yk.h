/**
 * @file point_yk.h
 * @brief 遥控点模型定义（控制量）
 */
#ifndef EMS_MODEL_POINT_YK_H
#define EMS_MODEL_POINT_YK_H

#include <string>
#include <vector>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

class PointYk : public BasePoint {
public:
    PointYk() = default;
    explicit PointYk(int64_t id) : BasePoint(id) {}

    int64_t channel_id{0};

    std::string decode_code;
    int bit{0};
    YKCommandType command_type{YKCommandType::Pulse};

    int64_t related_yx_id{0};

    int iec_common_address{0};
    int iec_cot{3};
    int iec_quality{0};

    std::string description;

    json to_json() const override {
        json j = BasePoint::to_json();
        j["channel_id"] = channel_id;
        j["point_type"] = "YK";
        j["decode_code"] = decode_code;
        j["bit"] = bit;
        j["command_type"] = static_cast<int>(command_type);
        j["command_type_name"] = get_command_type_name();
        j["related_yx_id"] = related_yx_id;
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
        if (j.contains("command_type")) {
            command_type = static_cast<YKCommandType>(j["command_type"].get<int>());
        }
        if (j.contains("related_yx_id")) related_yx_id = j["related_yx_id"].get<int64_t>();
        if (j.contains("iec_common_address")) iec_common_address = j["iec_common_address"].get<int>();
        if (j.contains("iec_cot")) iec_cot = j["iec_cot"].get<int>();
        if (j.contains("iec_quality")) iec_quality = j["iec_quality"].get<int>();
        if (j.contains("description")) description = j["description"].get<std::string>();
    }

    std::string get_command_type_name() const {
        switch (command_type) {
            case YKCommandType::Pulse: return "Pulse";
            case YKCommandType::Latch: return "Latch";
            case YKCommandType::Step: return "Step";
            default: return "Unknown";
        }
    }
};

using PointYKList = std::vector<std::shared_ptr<PointYk>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_POINT_YK_H
