#pragma once
#include <set>
#include <string>

#include "web/transport/json.h"

namespace ems::web {
struct Principal {
    std::string subject_id;
    std::set<std::string> permissions;
    std::set<std::string> station_ids;
    std::set<std::string> device_roots;
    /**
     * @brief 检查明确操作权限；星号仅由部署配置授予。
     * @param[in] permission 操作权限。
     */
    void require(const std::string& permission) const;
};
}  // namespace ems::web
