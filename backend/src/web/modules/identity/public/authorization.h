#pragma once
#include <set>
#include <string>
#include <vector>

#include "web/transport/json.h"

namespace ems::web {
struct Principal {
    std::string subject_id;
    std::set<std::string> permissions;
    std::set<std::string> station_ids;
    std::set<std::string> device_roots;
    std::string session_id, kind, role_code, scope_mode;

    struct Scope {
        std::string station_id, device_root_id;
    };

    std::vector<Scope> scopes;
    /**
     * @brief 检查明确操作权限；星号仅在显式测试注入的主体中支持。
     * @param[in] permission 操作权限。
     */
    void require(const std::string& permission) const;
};
}  // namespace ems::web
