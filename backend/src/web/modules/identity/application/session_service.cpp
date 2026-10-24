#include "session_service.h"

#include "web/platform/crypto.h"

namespace ems::web {
void Principal::require(const std::string& permission) const {
    if (!permissions.count("*") && !permissions.count(permission)) {
        throw ApiError(403, "PermissionDenied", "permission_denied");
    }
}

SessionService::SessionService(Json users, std::size_t capacity, int ttl)
    : users_(std::move(users)), capacity_(capacity), ttl_(ttl) {
    if (!users_.is_array() || users_.empty() || users_.size() > 1000 || capacity_ == 0 ||
        ttl_ <= 0) {
        throw std::invalid_argument("invalid identity configuration");
    }

    // 只验证编码结构，不将部署账号或哈希暴露给 HTTP 响应。
    std::set<std::string> names;
    for (const auto& user : users_) {
        fields(user, {"username", "password_hash", "permissions", "station_ids", "device_roots"});
        auto name = text(user, "username", 64);
        auto hash = text(user, "password_hash", 512);
        if (!names.insert(name).second || hash.rfind("pbkdf2_sha256$", 0) != 0) {
            throw std::invalid_argument("invalid identity account");
        }
        for (auto key : {"permissions", "station_ids", "device_roots"}) {
            if (!user.count(key) || !user.at(key).is_array())
                throw std::invalid_argument("invalid account scope");
            for (const auto& value : user.at(key))
                if (!value.is_string()) throw std::invalid_argument("invalid account scope");
        }
    }
}

std::pair<std::string, Session> SessionService::login(const Json& request) {
    fields(request, {"username", "password"});
    const auto username = text(request, "username", 64);
    const auto password = text(request, "password", 1024);
    const auto now = std::chrono::steady_clock::now();

    // 在锁内记录准入，密码派生放在锁外，避免一个慢登录阻塞已有会话。
    {
        std::lock_guard<std::mutex> guard(mutex_);
        for (auto it = attempts_.begin(); it != attempts_.end();) {
            if (it->second.until <= now)
                it = attempts_.erase(it);
            else
                ++it;
        }
        if (attempts_.size() >= 1000 && !attempts_.count(username))
            throw ApiError(429, "ResourceExhausted", "login_rate_limited");
        auto& attempt = attempts_[username];
        if (attempt.count == 0) attempt.until = now + std::chrono::minutes(1);
        if (++attempt.count > 5) throw ApiError(429, "ResourceExhausted", "login_rate_limited");
    }

    const Json* account = nullptr;
    for (const auto& user : users_)
        if (user.at("username") == username) account = &user;

    // 不存在账号也执行同成本派生，减少用户名探测时序差异。
    static const auto dummy = hash_password(random_token());
    const auto encoded = account ? account->at("password_hash").get<std::string>() : dummy;
    const bool valid = verify_password(password, encoded);
    if (!account || !valid) throw ApiError(401, "PermissionDenied", "invalid_credentials");

    Principal principal;
    principal.subject_id = username;
    for (const auto& value : account->at("permissions"))
        principal.permissions.insert(value.get<std::string>());
    for (const auto& value : account->at("station_ids"))
        principal.station_ids.insert(value.get<std::string>());
    for (const auto& value : account->at("device_roots"))
        principal.device_roots.insert(value.get<std::string>());

    auto token = random_token();
    Session session{std::move(principal), random_token(), now + std::chrono::seconds(ttl_)};
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.expires <= now)
            it = sessions_.erase(it);
        else
            ++it;
    }
    if (sessions_.size() >= capacity_) throw ApiError(503, "ResourceExhausted", "session_capacity");
    sessions_.emplace(token, session);
    return {token, session};
}

Session SessionService::get(const std::string& token) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = sessions_.find(token);
    if (found == sessions_.end()) throw ApiError(401, "PermissionDenied", "unauthenticated");

    if (found->second.expires <= std::chrono::steady_clock::now()) {
        sessions_.erase(found);
        throw ApiError(401, "PermissionDenied", "session_expired");
    }
    return found->second;
}

void SessionService::logout(const std::string& token) {
    std::lock_guard<std::mutex> guard(mutex_);
    sessions_.erase(token);
}
}  // namespace ems::web
