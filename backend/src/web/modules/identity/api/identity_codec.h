#pragma once
#include "web/modules/identity/application/authorization_service.h"

namespace ems::web {
/** @brief 校验固定用户与自助路由的字段白名单。
 * @param[in] path 固定路径。 @param[in] body 请求对象。
 */
void validate_identity_request(const std::string& path, const Json& body);
/** @brief 读取合法 UTF-8 并按 Unicode 码点计数。
 * @param[in] body 对象。 @param[in] key 字段。 @param[in] maximum 字符上限。
 * @param[in] allow_empty 允许空值。 @param[in] allow_line_breaks 允许纯文本通知中的换行。
 * @return 原样字符串，非法输入抛 ApiError。
 */
std::string identity_text(const Json& body, const char* key, std::size_t maximum,
                          bool allow_empty = false, bool allow_line_breaks = false);
/** @brief 校验 ASCII 用户名。 @param[in] username 用户名。 */
void validate_username(const std::string& username);
/** @brief 校验统一密码规则，不做截断或归一化。
 * @param[in] password 密码。 @param[in] username 用户名。 @param[in] minimum 策略最小长度。
 */
void validate_password(const std::string& password, const std::string& username, int minimum);
/** @brief 读取固定枚举字段。
 * @param[in] body 对象。 @param[in] key 字段。 @param[in] choices 允许值。
 * @return 固定值或格式错误。
 */
std::string identity_enum(const Json& body, const char* key,
                          std::initializer_list<const char*> choices);
/** @brief 分页参数校验。
 * @param[in] body 对象。 @return page 和 page_size。
 */
std::pair<int, int> identity_page(const Json& body);
/** @brief 公开用户与编辑摘要。
 * @param[in] auth 共享摘要服务。 @param[in] user 用户。 @param[in] scopes 当前范围。
 * @return 不含内部主键、哈希的 DTO。
 */
Json public_user(const AuthorizationService& auth, const database::UserRecord& user,
                 const std::vector<database::ScopeRecord>& scopes);
/** @brief 公开策略及摘要。
 * @param[in] auth 共享服务。 @param[in] policy 策略。 @return DTO。
 */
Json public_policy(const AuthorizationService& auth, const database::LoginPolicyRecord& policy);
/** @brief 锁行后检查编辑摘要。
 * @param[in] body 请求。 @param[in] current 当前公开 DTO。
 */
void expect_identity_edit(const Json& body, const Json& current);
}  // namespace ems::web
