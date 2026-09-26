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

/** @brief 保存目标点与来源点的映射和计算公式。 */
class PointMapping : public BaseModelWithId, public TimestampMixin, public StatusMixin {
   public:
    /** @brief 构造默认点映射。 */
    PointMapping() = default;

    /**
     * @brief 从数据库 ID 构造点映射。
     * @param[in] id 对象标识。
     */
    explicit PointMapping(int64_t id) : BaseModelWithId(id) {}

    std::string device_name;
    std::string target_point_code;
    std::vector<std::string> source_point_codes;
    std::string formula;
    std::string description;

    /**
     * @copydoc BaseModel::to_json
     * @return 序列化后的 JSON 数据。
     */
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

    /**
     * @copydoc BaseModel::from_json
     * @param[in] j 元素索引。
     */
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

    /**
     * @brief 返回便于展示的来源点及计算公式文本。
     * @return 对应的文本内容。
     */
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
