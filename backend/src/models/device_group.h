/**
 * @file device_group.h
 * @brief 设备组模型定义
 */
#ifndef EMS_MODEL_DEVICE_GROUP_H
#define EMS_MODEL_DEVICE_GROUP_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base.h"

namespace ems {
namespace model {

/** @brief 保存设备分组及其子分组和设备引用。 */
class DeviceGroup : public BaseModelWithId, public TimestampMixin, public StatusMixin {
   public:
    /** @brief 构造默认设备分组。 */
    DeviceGroup() = default;

    /**
     * @brief 从数据库 ID 构造设备分组。
     * @param[in] id 对象标识。
     */
    explicit DeviceGroup(int64_t id) : BaseModelWithId(id) {}

    std::string code;
    std::string name;
    std::optional<int64_t> parent_id;
    std::optional<std::string> description;

    std::vector<std::shared_ptr<DeviceGroup>> children;
    std::vector<int64_t> device_ids;

    /**
     * @copydoc BaseModel::to_json
     * @return 序列化后的 JSON 数据。
     */
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["code"] = code;
        j["name"] = name;
        j["parent_id"] = parent_id.has_value() ? json(parent_id.value()) : json(nullptr);
        j["description"] = description.has_value() ? json(description.value()) : json(nullptr);
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();

        json children_arr = json::array();
        for (const auto& child : children) {
            children_arr.push_back(child->to_json());
        }
        j["children"] = children_arr;
        j["device_count"] = device_ids.size();

        return j;
    }

    /**
     * @copydoc BaseModel::from_json
     * @param[in] j 元素索引。
     */
    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);

        if (j.contains("code")) code = j["code"].get<std::string>();
        if (j.contains("name")) name = j["name"].get<std::string>();
        if (j.contains("parent_id") && !j["parent_id"].is_null()) {
            parent_id = j["parent_id"].get<int64_t>();
        }
        if (j.contains("description") && !j["description"].is_null()) {
            description = j["description"].get<std::string>();
        }
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }

    /**
     * @brief 返回当前分组名称作为显示路径。
     * @return 对应的文本内容。
     */
    std::string get_full_path() const { return name; }

    /**
     * @brief 返回当前分组直接关联的设备数量。
     * @return 处理或统计得到的数据项数量。
     */
    int get_device_count() const { return static_cast<int>(device_ids.size()); }
};

using DeviceGroupList = std::vector<std::shared_ptr<DeviceGroup>>;

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_DEVICE_GROUP_H
