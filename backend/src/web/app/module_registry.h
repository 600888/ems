#pragma once
#include <string>
#include <vector>

namespace ems::web {
struct Route {
    std::string path;
    bool post;
};

/** @brief 返回显式设备接口表，不使用动态路径参数。
 * @return 固定路由集合。
 */
const std::vector<Route>& device_routes();
}  // namespace ems::web
