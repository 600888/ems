#pragma once
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "web/modules/identity/public/authorization.h"

namespace ems::web {
struct Session {
    Principal principal;
    std::string csrf;
    std::chrono::steady_clock::time_point expires;
};

class SessionService {
   public:
    /**
     * @brief 加载部署账号、哈希和授权；不接受明文或内置账号。
     * @param[in] users 账号配置数组。
     * @param[in] max_sessions 内存会话容量。
     * @param[in] ttl_seconds 会话有效时长。
     */
    SessionService(Json users, std::size_t max_sessions, int ttl_seconds);
    /**
     * @brief 校验密码并创建安全随机会话；错误与账号不存在一致。
     * @param[in] request username/password JSON。
     * @return 会话令牌与会话内容；容量/频率限制抛 ApiError。
     */
    std::pair<std::string, Session> login(const Json& request);
    /**
     * @brief 读取未过期会话。
     * @param[in] token cookie 令牌。
     * @return 主体与 CSRF 信息；不存在返回 401。
     */
    Session get(const std::string& token);
    /**
     * @brief 注销当前会话。
     * @param[in] token cookie 令牌。
     */
    void logout(const std::string& token);

   private:
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
