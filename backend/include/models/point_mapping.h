/**
 * @file point_mapping.h
 * @brief 测点映射模型定义（计算测点）
 */
#ifndef EMS_MODEL_POINT_MAPPING_H
#define EMS_MODEL_POINT_MAPPING_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base.h"

namespace ems {
namespace model {

class PointMapping : public BaseModelWithId, public TimestampMixin, public StatusMixin {
   public:
    PointMapping() = default;
    explicit PointMapping(int64_t id) : BaseModelWithId(id) {}

    std::string device_name;
    std::string target_point_code;
    std::vector<std::string> source_point_codes;
    std::string formula;
    std::string description;

    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["device_name"] = device_name;
        j["target_point_code"] = target_point_code;
        j["source_point_codes"] = source_point_codes;
        j["formula"] = formula;
        j["description"] = description;
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();
        return j;
    }

    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);

        if (j.contains("device_name")) device_name = j["device_name"].get<std::string>();
        if (j.contains("target_point_code"))
            target_point_code = j["target_point_code"].get<std::string>();
        if (j.contains("source_point_codes")) {
            source_point_codes = j["source_point_codes"].get<std::vector<std::string>>();
        }
        if (j.contains("formula")) formula = j["formula"].get<std::string>();
        if (j.contains("description") && !j["description"].is_null()) {
            description = j["description"].get<std::string>();
        }
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }

    std::string to_formula_string() const {
        std::string result = target_point_code + " = " + formula + " (";
        for (size_t i = 0; i < source_point_codes.size(); ++i) {
            if (i > 0) result += ", ";
            result += source_point_codes[i];
        }
        result += ")";
        return result;
    }
};

using PointMappingList = std::vector<std::shared_ptr<PointMapping>>;

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_POINT_MAPPING_H
