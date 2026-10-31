#pragma once
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "authorization_service.h"
#include "web/modules/identity/public/authorization.h"

namespace ems::web {
struct Session {
    Principal principal;
    std::string csrf;
    std::chrono::steady_clock::time_point expires;
    Json data;
};

class SessionService {
   public:
    /**
     * @brief 创建内存会话服务；账号来源由调用方注入，不读取部署配置。
     * @param[in] users 显式注入的账号数组；为空时服务保持未装配状态并拒绝登录。
     * @param[in] max_sessions 内存会话容量。
     * @param[in] ttl_seconds 会话有效时长。
     */
    SessionService(Json users, std::size_t max_sessions, int ttl_seconds);
    /** @brief 创建生产持久身份服务。
     * @param[in] authorization 公共身份服务和仓储，共享所有权。
     */
    explicit SessionService(std::shared_ptr<AuthorizationService> authorization);
    /**
     * @brief 校验密码并创建安全随机会话；错误与账号不存在一致。
     * @param[in] request username/password JSON。
     * @param[in] call 调用期限、真实来源 IP 和有界客户端描述。
     * @return 会话令牌与会话内容；账号未装配、容量或频率限制时抛 ApiError。
     */
    std::pair<std::string, Session> login(const Json& request, const IdentityCall& call = {});
    /**
     * @brief 读取未过期会话。
     * @param[in] token cookie 令牌。
     * @param[in] call 调用期限及来源。
     * @return 主体与 CSRF 信息；不存在返回 401。
     */
    Session get(const std::string& token, const IdentityCall& call = {});
    /**
     * @brief 注销当前会话。
     * @param[in] token cookie 令牌。
     * @param[in] call 调用期限及来源。
     */
    void logout(const std::string& token, const IdentityCall& call = {});
    /** @brief 执行持久会话相关用例；HTTP 入口已校验 Origin/CSRF。
     * @param[in] path 固定路径。 @param[in] token Cookie。 @param[in] body 白名单请求。
     * @param[in] call 调用信息。 @return 公开 DTO。
     */
    Json execute(const std::string& path, const std::string& token, const Json& body,
                 const IdentityCall& call);

    /** @brief 读取生产授权服务。 @return 测试模式为空，生产模式为共享服务。 */
    std::shared_ptr<AuthorizationService> authorization() const { return authorization_; }

   private:
    std::shared_ptr<AuthorizationService> authorization_;
    std::string dummy_;
    /** @brief 来源及凭据限流，不持有密码派生锁。
     * @param[in] key 来源键。 @param[in] limit 每分钟上限。
     */
    void rate(const std::string& key, int limit);
    /** @brief 持久登录。 @param[in] request 凭据。 @param[in] call 调用信息。
     * @return Cookie 和公开会话。
     */
    std::pair<std::string, Session> persistent_login(const Json& request, const IdentityCall& call);
    Json users_;
    std::size_t capacity_;
    int ttl_;
    std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;

    struct Attempt {
        int count{0};
        std::chrono::steady_clock::time_point until;
    };

    std::unordered_map<std::string, Attempt> attempts_;
};
}  // namespace ems::web
