#pragma once
#include <mutex>
#include <unordered_map>

#include "authorization_service.h"

namespace ems::web {
class UserService {
   public:
    /** @brief 装配用户用例。 @param[in] authorization 公共授权服务。 */
    explicit UserService(std::shared_ptr<AuthorizationService> authorization);
    /** @brief 执行固定用户或自助路由；所有写入使用短事务。
     * @param[in] path 路由。 @param[in] token Cookie。 @param[in] body 请求。
     * @param[in] call 期限和来源。 @return 公开业务 DTO。
     */
    Json execute(const std::string& path, const std::string& token, const Json& body,
                 const IdentityCall& call);
    /** @brief 受控主机初始化或恢复；不通过 HTTP 暴露。
     * @param[in] username 用户名。 @param[in] display_name 初始姓名。
     * @param[in] password 交互输入的临时密码。 @param[in] recover 是否恢复已有账号。
     * @param[in] call 调用信息。 @return 已脱敏用户。
     */
    Json provision(const std::string& username, const std::string& display_name,
                   const std::string& password, bool recover, const IdentityCall& call);
    /** @brief 分批执行会话与审计保留策略。
     * @param[in] call 调用期限，每次最多各删除 200 条。
     * @param[in] audit_retention_days 审计保留天数，1–3650。
     */
    void maintain(const IdentityCall& call, int audit_retention_days = 180);

   private:
    std::shared_ptr<AuthorizationService> auth_;
    std::mutex rate_mutex_;

    struct Rate {
        int count{0};
        std::chrono::steady_clock::time_point until;
    };

    std::unordered_map<std::string, Rate> rates_;
    /** @brief 每用户每分钟最多 60 个管理或自助变更。
     * @param[in] user_id 已认证用户；计数集合有界。
     */
    void admit(const std::string& user_id);
    /** @brief 读取完整范围目录，不截断。 @param[in,out] tx 事务。
     * @param[in] call 期限。 @return 最多 500 节点的树。
     */
    Json scope_tree(database::ITransaction& tx, const IdentityCall& call);
    /** @brief 按真实设备关系校验并规范化授权。
     * @param[in,out] tx 事务。 @param[in] body 角色、模式、完整范围。
     * @param[in] call 期限。 @return 规范化范围。
     */
    std::vector<database::ScopeRecord> normalize_scopes(database::ITransaction& tx,
                                                        const Json& body, const IdentityCall& call);
};
}  // namespace ems::web
