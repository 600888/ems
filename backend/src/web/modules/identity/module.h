#pragma once
#include "application/session_service.h"
#include "application/user_service.h"
#include "web/app/module_registry.h"

namespace ems::web {
/** @brief 返回身份模块固定路由。 @return 精确方法与路径集合。 */
const std::vector<Route>& identity_routes();
/** @brief 从受限文件加载跨实例摘要密钥，不提供默认值。
 * @param[in] path 文件路径；空值读取 EMS_WEB_EDIT_KEY_FILE。
 * @return 至少 32 字节密钥；缺失或尺寸非法时抛异常。
 */
std::string load_identity_key(const std::string& path);
}  // namespace ems::web
