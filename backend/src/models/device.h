/**
 * @file device.h
 * @brief 设备模型定义
 */
#ifndef EMS_MODEL_DEVICE_H
#define EMS_MODEL_DEVICE_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base.h"

namespace ems {
namespace model {

/** @brief 保存设备身份、类型和所属通道。 */
class device : public BaseModelWithId, public TimestampMixin, public StatusMixin {
   public:
    /** @brief 构造默认设备模型。 */
    device() = default;

    /**
     * @brief 从数据库 ID 构造设备模型。
     * @param[in] id 对象标识。
     */
    explicit device(int64_t id) : BaseModelWithId(id) {}

    std::string code;
    std::string name;
    DeviceType device_type{DeviceType::RTU};
    std::optional<int64_t> group_id;
    std::optional<std::string> description;

    std::vector<std::shared_ptr<class channel>> channels;

    /**
     * @copydoc BaseModel::to_json
     * @return 序列化后的 JSON 数据。
     */
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["code"] = code;
        j["name"] = name;
        j["device_type"] = static_cast<int>(device_type);
        j["device_type_name"] = get_device_type_name();
        j["group_id"] = group_id.has_value() ? json(group_id.value()) : json(nullptr);
        j["description"] = description.has_value() ? json(description.value()) : json(nullptr);
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();
        j["channel_count"] = channels.size();

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
        if (j.contains("device_type")) {
            device_type = static_cast<DeviceType>(j["device_type"].get<int>());
        }
        if (j.contains("group_id") && !j["group_id"].is_null()) {
            group_id = j["group_id"].get<int64_t>();
        }
        if (j.contains("description") && !j["description"].is_null()) {
            description = j["description"].get<std::string>();
        }
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }

    /**
     * @brief 返回设备类型的人类可读名称。
     * @return 对应的文本内容。
     */
    std::string get_device_type_name() const {
        switch (device_type) {
            case DeviceType::RTU:
                return "RTU";
            case DeviceType::Protection:
                return "Protection";
            case DeviceType::Meter:
                return "Meter";
            case DeviceType::IED:
                return "IED";
            case DeviceType::PLC:
                return "PLC";
            case DeviceType::Virtual:
                return "Virtual";
            case DeviceType::Gateway:
                return "Gateway";
            case DeviceType::Other:
                return "Other";
            default:
                return "Unknown";
        }
    }

    /**
     * @brief 返回设备关联的通道数量。
     * @return 处理或统计得到的数据项数量。
     */
    int get_channel_count() const { return static_cast<int>(channels.size()); }
};

using DeviceList = std::vector<std::shared_ptr<device>>;

}  // namespace model
}  // namespace ems

#endif  // EMS_MODEL_DEVICE_H
