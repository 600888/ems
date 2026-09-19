/**
 * @file slave.h
 * @brief 从机模型定义
 */
#ifndef EMS_MODEL_SLAVE_H
#define EMS_MODEL_SLAVE_H

#include <string>
#include <memory>
#include "base.h"

namespace ems {
namespace model {

class Slave : public BaseModelWithId, public TimestampMixin, public StatusMixin {
public:
    Slave() = default;
    explicit Slave(int id) : BaseModelWithId(id) {}
    
    int channel_id{0};
    int slave_id{1};
    std::string name;
    std::string description;
    
    json to_json() const override {
        json j = BaseModelWithId::to_json();
        j["channel_id"] = channel_id;
        j["slave_id"] = slave_id;
        j["name"] = name;
        j["description"] = description;
        j["timestamp"] = TimestampMixin::to_json();
        j["status"] = StatusMixin::to_json();
        return j;
    }
    
    void from_json(const json& j) override {
        BaseModelWithId::from_json(j);
        
        if (j.contains("channel_id") && !j["channel_id"].is_null()) {
            channel_id = j["channel_id"].get<int64_t>();
        }
        if (j.contains("slave_id")) slave_id = j["slave_id"].get<int>();
        if (j.contains("name")) name = j["name"].get<std::string>();
        if (j.contains("description") && !j["description"].is_null()) {
            description = j["description"].get<std::string>();
        }
        if (j.contains("timestamp")) TimestampMixin::from_json(j["timestamp"]);
        if (j.contains("status")) StatusMixin::from_json(j["status"]);
    }
};

using SlaveList = std::vector<std::shared_ptr<Slave>>;

} // namespace model
} // namespace ems

#endif // EMS_MODEL_SLAVE_H
