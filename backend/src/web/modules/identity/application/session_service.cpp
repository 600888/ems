#include "session_service.h"

#include <algorithm>

#include "web/modules/identity/api/identity_codec.h"
#include "web/platform/crypto.h"

namespace ems::web {
void Principal::require(const std::string& permission) const {
    if (!permissions.count("*") && !permissions.count(permission)) {
        throw ApiError(403, "PermissionDenied", "permission_denied");
    }
}

SessionService::SessionService(Json users, std::size_t capacity, int ttl)
    : users_(std::move(users)), capacity_(capacity), ttl_(ttl) {
    if (!users_.is_array() || users_.size() > 1000 || capacity_ == 0 || ttl_ <= 0) {
        throw std::invalid_argument("invalid identity configuration");
    }

    // 只验证编码结构，不将部署账号或哈希暴露给 HTTP 响应。
    std::set<std::string> names;
    for (const auto& user : users_) {
        fields(user, {"username", "password_hash", "permissions", "station_ids", "device_roots"});
        auto name = text(user, "username", 64);
        auto hash = text(user, "password_hash", 512);
        if (!names.insert(name).second ||
            (hash.rfind("pbkdf2_sha256$", 0) != 0 && hash.rfind("$argon2id$", 0) != 0)) {
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

std::pair<std::string, Session> SessionService::login(const Json& request,
                                                      const IdentityCall& call) {
    if (authorization_) return persistent_login(request, call);
    if (users_.empty()) throw ApiError(503, "NotReady", "identity_not_configured");
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

Session SessionService::get(const std::string& token, const IdentityCall& call) {
    if (authorization_) {
        auto tx = identity_value(authorization_->db.database->begin(call.context));
        auto state = authorization_->authenticate(*tx, token, database::IdentityLock::Share, call);
        auto result = Session{
            state.principal, state.session.csrf_token, {}, authorization_->session_json(state)};
        identity_value(tx->rollback(call.context));
        return result;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = sessions_.find(token);
    if (found == sessions_.end()) throw ApiError(401, "PermissionDenied", "unauthenticated");

    if (found->second.expires <= std::chrono::steady_clock::now()) {
        sessions_.erase(found);
        throw ApiError(401, "PermissionDenied", "session_expired");
    }
    return found->second;
}

void SessionService::logout(const std::string& token, const IdentityCall& call) {
    if (authorization_) {
        execute("/api/auth/logout", token, Json::object(), call);
        return;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    sessions_.erase(token);
}

SessionService::SessionService(std::shared_ptr<AuthorizationService> authorization)
    : authorization_(std::move(authorization)), users_(Json::array()), capacity_(0), ttl_(0) {
    if (!authorization_) throw std::invalid_argument("identity dependencies");
    dummy_ = hash_password(random_token());
}

void SessionService::rate(const std::string& key, int limit) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        if (it->second.until <= now)
            it = attempts_.erase(it);
        else
            ++it;
    }
    if (attempts_.size() >= 10000 && !attempts_.count(key))
        throw ApiError(429, "ResourceExhausted", "rate_limited");
    auto& attempt = attempts_[key];
    if (attempt.count == 0) attempt.until = now + std::chrono::minutes(1);
    if (++attempt.count > limit) throw ApiError(429, "ResourceExhausted", "rate_limited");
}

std::pair<std::string, Session> SessionService::persistent_login(const Json& request,
                                                                 const IdentityCall& call) {
    using namespace database;
    auto& a = *authorization_;
    fields(request, {"username", "password"});
    auto name = identity_text(request, "username", 64);
    auto password = identity_text(request, "password", 256);
    rate("ip:" + call.source_ip, 30);
    // 单独限制未知用户名的计数空间；所有未知账号仍执行同成本密码校验。
    auto read = identity_value(a.db.database->begin(call.context));
    const bool ascii_name =
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c < 127; });
    auto snapshot =
        ascii_name
            ? identity_value(a.db.users->user(*read, name, true, IdentityLock::None, call.context))
            : std::optional<UserRecord>{};
    auto read_policy = identity_value(a.db.users->policy(*read, false, call.context));
    identity_value(read->rollback(call.context));
    if (!snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = "unknown:" + sha256(name);
        if (attempts_.size() >= 10000 && !attempts_.count(key))
            throw ApiError(429, "ResourceExhausted", "rate_limited");
        auto& attempt = attempts_[key];
        if (attempt.count == 0)
            attempt.until =
                std::chrono::steady_clock::now() + std::chrono::minutes(read_policy.lock_minutes);
        attempt.count = std::min(attempt.count + 1, read_policy.failed_login_limit);
    }
    bool verified = verify_password(password, snapshot ? snapshot->password_hash : dummy_);
    auto tx = identity_value(a.db.database->begin(call.context));
    auto policy = identity_value(a.db.users->policy(*tx, false, call.context));
    auto user =
        ascii_name
            ? identity_value(a.db.users->user(*tx, name, true, IdentityLock::Update, call.context))
            : std::optional<UserRecord>{};
    const auto now = identity_time();
    bool unchanged = snapshot && user && snapshot->password_hash == user->password_hash &&
                     snapshot->role_code == user->role_code &&
                     snapshot->scope_mode == user->scope_mode &&
                     snapshot->must_change_password == user->must_change_password &&
                     snapshot->status == user->status &&
                     snapshot->temporary_password_expires_at == user->temporary_password_expires_at;
    bool ok = verified && unchanged && user->status == "enabled" &&
              (!user->locked_until || *user->locked_until <= now) &&
              (!user->must_change_password ||
               (user->temporary_password_expires_at && *user->temporary_password_expires_at > now));
    if (!ok) {
        if (user && user->status == "enabled" &&
            (!user->locked_until || *user->locked_until <= now)) {
            if (user->locked_until) {
                user->failed_attempts = 0;
                user->locked_until.reset();
            }
            user->failed_attempts = std::min(user->failed_attempts + 1, policy.failed_login_limit);
            if (user->failed_attempts >= policy.failed_login_limit)
                user->locked_until = identity_time(policy.lock_minutes * 60);
            identity_value(a.db.users->save_user(*tx, *user, call.context));
            if (user->locked_until)
                a.audit(*tx, call, "", user->user_id, "auth.lock", "failure",
                        "invalid_credentials");
        }
        a.audit(*tx, call, "", user ? user->user_id : "", "auth.login", "failure",
                "invalid_credentials");
        identity_value(tx->commit(call.context));
        throw ApiError(401, "Unauthenticated", "invalid_credentials");
    }
    auto active = identity_value(a.db.users->sessions(*tx, user->user_id, true, call.context));
    int count = 0;
    for (auto& s : active) {
        bool expired = s.absolute_expires_at <= now ||
                       identity_add(s.last_interactive_at, policy.idle_timeout_minutes * 60) <= now;
        if (expired || s.kind == "password_change") {
            s.revoked_at = now;
            s.revoke_reason = expired ? "expired" : "new_login";
            identity_value(a.db.users->save_session(*tx, s, call.context));
        } else
            ++count;
    }
    for (auto& s : active)
        if (count >= 3 && !s.revoked_at && s.kind == "full") {
            s.revoked_at = now;
            s.revoke_reason = "session_limit";
            identity_value(a.db.users->save_session(*tx, s, call.context));
            --count;
            a.audit(*tx, call, user->user_id, user->user_id, "session.revoke", "success",
                    "session_limit", {{"session_id", s.session_id}});
        }
    auto token = random_token();
    SessionRecord session;
    session.session_id = random_token();
    session.user_id = user->user_id;
    session.kind = user->must_change_password ? "password_change" : "full";
    session.token_hash = digest_bytes(sha256(token));
    session.csrf_token = random_csrf();
    session.created_at = session.auth_time = session.last_interactive_at = now;
    session.absolute_expires_at = identity_time(user->must_change_password ? 300 : 8 * 3600);
    if (!call.source_ip.empty()) session.source_ip = call.source_ip;
    session.user_agent = call.user_agent;
    user->failed_attempts = 0;
    user->locked_until.reset();
    user->last_login_at = now;
    identity_value(a.db.users->save_user(*tx, *user, call.context));
    identity_value(a.db.users->save_session(*tx, session, call.context));
    a.audit(*tx, call, user->user_id, user->user_id, "auth.login", "success", "",
            {{"session_id", session.session_id}, {"kind", session.kind}});
    AuthState state{*user,
                    session,
                    policy,
                    identity_value(a.db.users->scopes(*tx, user->user_id, call.context)),
                    {}};
    state.principal = a.principal(state);
    auto data = a.session_json(state);
    a.valid(state);
    identity_value(tx->commit(call.context));
    return {token, Session{state.principal, session.csrf_token, {}, data}};
}

Json SessionService::execute(const std::string& path, const std::string& token, const Json& body,
                             const IdentityCall& call) {
    using namespace database;
    auto& a = *authorization_;
    if (path == "/api/auth/login-options") {
        fields(body, {});
        auto tx = identity_value(a.db.database->begin(call.context));
        auto p = identity_value(a.db.users->policy(*tx, false, call.context));
        identity_value(tx->rollback(call.context));
        return {{"login_notice", p.login_notice},
                {"password_min_length", p.password_min_length},
                {"password_max_length", 32}};
    }
    if (path == "/api/auth/logout" && token.empty()) {
        fields(body, {});
        return {{"logged_out", true}};
    }
    auto tx = identity_value(a.db.database->begin(call.context));
    AuthState state;
    try {
        state = a.authenticate(*tx, token, IdentityLock::Update, call);
    } catch (const ApiError& error) {
        if (error.status == 401 && path == "/api/auth/logout") {
            fields(body, {});
            identity_value(tx->rollback(call.context));
            return {{"logged_out", true}};
        }
        throw;
    }
    if (path == "/api/auth/session") {
        fields(body, {});
        auto result = a.session_json(state);
        identity_value(tx->rollback(call.context));
        return result;
    }
    if (path != "/api/auth/password/complete" && path != "/api/auth/logout") a.full(state);
    if (path == "/api/auth/reauthenticate" || path == "/api/auth/password/complete" ||
        path == "/api/profile/change-password") {
        bool reauth = path == "/api/auth/reauthenticate";
        if (reauth)
            fields(body, {"password"});
        else
            fields(body, {"old_password", "new_password"});
        if (path == "/api/auth/password/complete" && state.session.kind != "password_change")
            throw ApiError(403, "PermissionDenied", "permission_denied");
        auto password = identity_text(body, reauth ? "password" : "old_password", 256);
        auto next = reauth ? std::string{} : identity_text(body, "new_password", 256);
        auto hash = state.user.password_hash;
        auto uid = state.user.user_id;
        if (!reauth) validate_password(next, state.user.username, state.policy.password_min_length);
        identity_value(tx->rollback(call.context));
        rate("ip:" + call.source_ip, 30);
        bool verified = verify_password(password, hash);
        if (!reauth && secure_equal(password, next))
            throw ApiError(422, "InvalidArgument", "password_policy_failed");
        auto next_hash = (!reauth && verified) ? hash_password(next) : std::string{};
        tx = identity_value(a.db.database->begin(call.context));
        state = a.authenticate(*tx, token, IdentityLock::Update, call);
        auto now = identity_time();
        if (!verified || state.user.password_hash != hash ||
            (state.user.locked_until && *state.user.locked_until > now)) {
            if (!state.user.locked_until || *state.user.locked_until <= now) {
                if (state.user.locked_until) state.user.failed_attempts = 0;
                state.user.failed_attempts =
                    std::min(state.user.failed_attempts + 1, state.policy.failed_login_limit);
                state.user.locked_until.reset();
                if (state.user.failed_attempts >= state.policy.failed_login_limit)
                    state.user.locked_until = identity_time(state.policy.lock_minutes * 60);
                identity_value(a.db.users->save_user(*tx, state.user, call.context));
            }
            a.audit(*tx, call, uid, uid, reauth ? "auth.reauthenticate" : "password.change",
                    "failure", "invalid_credentials");
            identity_value(tx->commit(call.context));
            throw ApiError(401, "Unauthenticated", "invalid_credentials");
        }
        const auto authorized = state;
        state.user.failed_attempts = 0;
        state.user.locked_until.reset();
        if (reauth) {
            state.session.auth_time = now;
            identity_value(a.db.users->save_session(*tx, state.session, call.context));
            identity_value(a.db.users->save_user(*tx, state.user, call.context));
            a.audit(*tx, call, uid, uid, "auth.reauthenticate");
            a.valid(authorized);
            identity_value(tx->commit(call.context));
            return {{"reauthenticated", true}, {"auth_time", identity_iso(now)}};
        }
        validate_password(next, state.user.username, state.policy.password_min_length);
        state.user.password_hash = next_hash;
        state.user.password_changed_at = now;
        state.user.updated_at = now;
        state.user.must_change_password = 0;
        state.user.temporary_password_expires_at.reset();
        identity_value(a.db.users->save_user(*tx, state.user, call.context));
        a.revoke_all(*tx, uid, "password_changed", call);
        a.audit(*tx, call, uid, uid, "password.change");
        a.valid(authorized);
        identity_value(tx->commit(call.context));
        return {{"reauthentication_required", true}};
    }
    fields(body, {});
    if (path == "/api/auth/activity") {
        // 多次快速交互合并写入，读取和轮询绝不续期。
        if (identity_add(state.session.last_interactive_at, 10) <= identity_time()) {
            state.session.last_interactive_at = identity_time();
            identity_value(a.db.users->save_session(*tx, state.session, call.context));
        }
        auto data = a.session_json(state);
        identity_value(tx->commit(call.context));
        return data;
    }
    if (path == "/api/auth/logout" || path == "/api/auth/logout-all") {
        if (path == "/api/auth/logout-all")
            a.revoke_all(*tx, state.user.user_id, "logout_all", call);
        else {
            state.session.revoked_at = identity_time();
            state.session.revoke_reason = "logout";
            identity_value(a.db.users->save_session(*tx, state.session, call.context));
        }
        a.audit(*tx, call, state.user.user_id, state.user.user_id,
                path == "/api/auth/logout" ? "auth.logout" : "auth.logout_all");
        identity_value(tx->commit(call.context));
        return {{"logged_out", true}};
    }
    throw ApiError(404, "NotFound", "route_not_found");
}
}  // namespace ems::web
