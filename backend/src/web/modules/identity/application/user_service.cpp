#include "user_service.h"

#include <algorithm>
#include <map>

#include "web/modules/identity/api/identity_codec.h"
#include "web/platform/crypto.h"

namespace ems::web {
using namespace database;

namespace {
std::string mask_ip(const std::optional<std::string>& ip) {
    if (!ip) return "";
    auto value = *ip;
    if (value.find(':') != std::string::npos) {
        auto end = value.find(':', value.find(':') + 1);
        return value.substr(0, end) + "::*";
    }
    auto end = value.rfind('.');
    return end == std::string::npos ? "hidden" : value.substr(0, end) + ".*";
}

std::string client_name(const std::string& ua) {
    std::string client = ua.find("Edg/") != std::string::npos       ? "Edge"
                         : ua.find("Firefox/") != std::string::npos ? "Firefox"
                         : ua.find("Chrome/") != std::string::npos  ? "Chrome"
                         : ua.find("Safari/") != std::string::npos  ? "Safari"
                                                                    : "Browser";
    if (ua.find("Windows") != std::string::npos)
        client += " / Windows";
    else if (ua.find("Android") != std::string::npos)
        client += " / Android";
    else if (ua.find("iPhone") != std::string::npos)
        client += " / iOS";
    else if (ua.find("Mac") != std::string::npos)
        client += " / macOS";
    else if (ua.find("Linux") != std::string::npos)
        client += " / Linux";
    return client;
}

std::string permission(const std::string& path) {
    if (path.rfind("/api/profile/", 0) == 0) return "";
    if (path.find("sessions/") != std::string::npos) return "session.manage";
    if (path.find("security-settings") != std::string::npos ||
        path == "/api/users/reset-password" || path == "/api/users/unlock")
        return "user.security";
    if (path == "/api/users/set-access") return "user.grant";
    if (path == "/api/users/create" || path == "/api/users/update" ||
        path == "/api/users/set-status" || path == "/api/users/delete")
        return "user.write";
    return "user.read";
}
}  // namespace

UserService::UserService(std::shared_ptr<AuthorizationService> authorization)
    : auth_(std::move(authorization)) {
    if (!auth_) throw std::invalid_argument("identity dependencies");
}

void UserService::admit(const std::string& user_id) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(rate_mutex_);
    for (auto it = rates_.begin(); it != rates_.end();) {
        if (it->second.until <= now)
            it = rates_.erase(it);
        else
            ++it;
    }
    if (rates_.size() >= 10000 && !rates_.count(user_id))
        throw ApiError(429, "ResourceExhausted", "rate_limited");
    auto& rate = rates_[user_id];
    if (rate.count == 0) rate.until = now + std::chrono::minutes(1);
    if (++rate.count > 60) throw ApiError(429, "ResourceExhausted", "rate_limited");
}

Json UserService::scope_tree(ITransaction& tx, const IdentityCall& call) {
    DeviceQuery q;
    q.limit = 500;
    q.order_by = "sort_order";
    auto page = identity_value(auth_->db.device_management->query(tx, q, call.context));
    if (page.total > 500) throw ApiError(422, "ResourceExhausted", "scope_tree_too_large");
    std::map<std::string, CfgDeviceRow> nodes;
    for (const auto& record : page.rows) {
        const auto& node = std::get<CfgDeviceRow>(record);
        nodes.emplace(node.device_id, node);
    }
    std::set<std::string> reached;
    std::function<Json(const std::string&, int)> build = [&](const std::string& parent,
                                                             int depth) -> Json {
        if (depth > 8) throw ApiError(503, "CorruptData", "identity_unavailable");
        Json list = Json::array();
        for (const auto& [id, row] : nodes)
            if (row.parent_device_id == parent) {
                if (!reached.insert(id).second)
                    throw ApiError(503, "CorruptData", "identity_unavailable");
                list.push_back({{"id", id},
                                {"name", row.name},
                                {"code", row.code},
                                {"kind", row.node_kind},
                                {"icon", row.node_kind == "station" ? "Building2" : "Box"},
                                {"children", build(id, depth + 1)}});
            }
        return list;
    };
    auto result = build("", 0);
    if (reached.size() != nodes.size()) throw ApiError(503, "CorruptData", "identity_unavailable");
    return result;
}

std::vector<ScopeRecord> UserService::normalize_scopes(ITransaction& tx, const Json& body,
                                                       const IdentityCall& call) {
    auto role = identity_enum(body, "role_code", {"admin", "operator", "observer"});
    auto mode = identity_enum(body, "scope_mode", {"all", "selected"});
    if (!body.contains("scopes") || !body.at("scopes").is_array() || body.at("scopes").size() > 200)
        throw ApiError(422, "InvalidArgument", "invalid_scope");
    if (mode == "all") {
        if (role != "admin" || !body.at("scopes").empty())
            throw ApiError(422, "InvalidArgument", "invalid_scope");
        return {};
    }
    std::vector<ScopeRecord> rows;
    std::set<std::string> stations;
    for (const auto& scope : body.at("scopes")) {
        fields(scope, {"station_id", "device_root_id"});
        ScopeRecord row;
        row.station_id = text(scope, "station_id", 64);
        if (!scope.contains("device_root_id"))
            throw ApiError(422, "InvalidArgument", "invalid_scope");
        if (!scope.at("device_root_id").is_null())
            row.device_root_id = text(scope, "device_root_id", 64);
        if (row.device_root_id == row.station_id) row.device_root_id.clear();
        rows.push_back(row);
        stations.insert(row.station_id);
    }
    std::map<std::string, CfgDeviceRow> nodes;
    auto node = [&](const std::string& id, bool lock = false) -> CfgDeviceRow {
        if (!lock && nodes.count(id)) return nodes.at(id);
        DeviceQuery q;
        q.filters = {{"device_id", id}};
        q.limit = 1;
        q.for_update = lock;
        auto page = identity_value(auth_->db.device_management->query(tx, q, call.context));
        if (page.rows.size() != 1) throw ApiError(422, "InvalidArgument", "invalid_scope");
        auto row = std::get<CfgDeviceRow>(page.rows.front());
        nodes[id] = row;
        return row;
    };
    // 用户与会话已锁定，站根按稳定 ID 顺序锁定后才读取父链。
    for (const auto& id : stations)
        if (node(id, true).node_kind != "station")
            throw ApiError(422, "InvalidArgument", "invalid_scope");
    std::map<std::pair<std::string, std::string>, std::set<std::string>> chains;
    for (const auto& row : rows) {
        auto cursor = row.device_root_id.empty() ? row.station_id : row.device_root_id;
        std::set<std::string> chain;
        bool valid = false;
        for (int depth = 0; depth <= 8; ++depth) {
            if (!chain.insert(cursor).second) break;
            auto current = node(cursor);
            if (current.node_kind == "station") {
                valid = current.device_id == row.station_id;
                break;
            }
            cursor = current.parent_device_id;
        }
        if (!valid) throw ApiError(422, "InvalidArgument", "invalid_scope");
        chains[{row.station_id, row.device_root_id}] = chain;
    }
    std::vector<ScopeRecord> normalized;
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return std::tie(a.station_id, a.device_root_id) < std::tie(b.station_id, b.device_root_id);
    });
    for (const auto& row : rows) {
        bool redundant = false;
        for (const auto& other : rows) {
            if (other.station_id != row.station_id || other.device_root_id == row.device_root_id)
                continue;
            if (other.device_root_id.empty() ||
                chains.at({row.station_id, row.device_root_id}).count(other.device_root_id))
                redundant = true;
        }
        for (const auto& old : normalized)
            if (old.station_id == row.station_id && old.device_root_id == row.device_root_id)
                redundant = true;
        if (!redundant) normalized.push_back(row);
    }
    return normalized;
}

Json UserService::execute(const std::string& path, const std::string& token, const Json& raw,
                          const IdentityCall& call) {
    auto& a = *auth_;
    validate_identity_request(path, raw);
    Json body = raw;
    bool profile = path.rfind("/api/profile/", 0) == 0;
    bool create = path == "/api/users/create", reset = path == "/api/users/reset-password";
    bool policy_change = path == "/api/users/security-settings/update";
    bool read = path == "/api/users/list" || path == "/api/users/detail" ||
                path == "/api/users/capabilities" || path == "/api/users/security-settings" ||
                path == "/api/profile/me" || path.find("sessions/list") != std::string::npos;
    if (create) {
        if (!body.contains("role_code")) body["role_code"] = "observer";
        if (!body.contains("status")) body["status"] = "disabled";
        if (!body.contains("scope_mode")) body["scope_mode"] = "selected";
        if (!body.contains("scopes")) body["scopes"] = Json::array();
    }
    auto tx = identity_value(a.db.database->begin(call.context));
    auto snapshot = a.authenticate(*tx, token, IdentityLock::Share, call);
    a.full(snapshot);
    auto needed = permission(path);
    if (!needed.empty()) snapshot.principal.require(needed);
    if (create) snapshot.principal.require("user.grant");
    if (!read) admit(snapshot.user.user_id);
    std::optional<UserRecord> target;
    if (!profile && !create && path != "/api/users/list" && path != "/api/users/capabilities" &&
        path != "/api/users/security-settings" && !policy_change) {
        auto id = identity_text(body, "user_id", 64);
        target = identity_value(a.db.users->user(*tx, id, false, IdentityLock::None, call.context));
        if (!target || target->status == "deleted")
            throw ApiError(404, "NotFound", "user_not_found");
    }
    std::string next_hash, next_password, original_hash;
    if (create || reset) {
        auto username = create ? identity_text(body, "username", 64) : target->username;
        validate_username(username);
        next_password = identity_text(body, "temporary_password", 256);
        validate_password(next_password, username, snapshot.policy.password_min_length);
        if (create) {
            identity_text(body, "display_name", 64);
            identity_enum(body, "role_code", {"admin", "operator", "observer"});
            identity_enum(body, "status", {"enabled", "disabled"});
        }
        if (reset ||
            (create && (body.at("role_code") == "admin" || body.at("status") == "enabled")))
            a.recent(snapshot);
        if (reset) {
            if (target->user_id == snapshot.user.user_id)
                throw ApiError(422, "InvalidArgument", "self_management_forbidden");
            original_hash = target->password_hash;
        }
        identity_value(tx->rollback(call.context));
        if (reset && verify_password(next_password, original_hash))
            throw ApiError(422, "InvalidArgument", "password_policy_failed");
        next_hash = hash_password(next_password);
        tx.reset();
    } else {
        identity_value(tx->rollback(call.context));
        tx.reset();
    }
    tx = identity_value(a.db.database->begin(call.context));
    // 所有管理员写入先锁策略；随后用户按内部 ID 排序，最后会话和站根。
    if ((!read && !profile) || path == "/api/users/list")
        identity_value(a.db.users->policy(*tx, true, call.context));
    if (!read || target) {
        std::vector<UserRecord> locks{snapshot.user};
        if (target && target->id != snapshot.user.id) locks.push_back(*target);
        std::sort(locks.begin(), locks.end(),
                  [](const auto& x, const auto& y) { return x.id < y.id; });
        for (const auto& user : locks)
            identity_value(a.db.users->user(*tx, user.user_id, false,
                                            read ? IdentityLock::Share : IdentityLock::Update,
                                            call.context));
    }
    auto state =
        a.authenticate(*tx, token, read ? IdentityLock::Share : IdentityLock::Update, call);
    a.full(state);
    if (!needed.empty()) state.principal.require(needed);
    if (create) state.principal.require("user.grant");
    if (target) {
        target = identity_value(
            a.db.users->user(*tx, target->user_id, false, IdentityLock::None, call.context));
        if (!target || target->status == "deleted")
            throw ApiError(404, "NotFound", "user_not_found");
        if (!read) identity_value(a.db.users->sessions(*tx, target->user_id, true, call.context));
    }
    auto dto = [&](const UserRecord& user) {
        return public_user(a, user,
                           identity_value(a.db.users->scopes(*tx, user.user_id, call.context)));
    };
    const auto commit_state = state;
    const bool needs_recent =
        !read && !profile && path != "/api/users/update" &&
        (!create || body.at("role_code") == "admin" || body.at("status") == "enabled");
    auto done = [&](Json value, bool commit = true) {
        if (commit) {
            a.valid(commit_state);
            if (needs_recent) a.recent(commit_state);
            identity_value(tx->commit(call.context));
        } else
            identity_value(tx->rollback(call.context));
        return value;
    };
    if (path == "/api/users/capabilities") {
        Json role_permissions;
        for (const auto& role : {"admin", "operator", "observer"})
            role_permissions[role] = a.permissions(role);
        return done({{"roles", {"admin", "operator", "observer"}},
                     {"role_permissions", role_permissions},
                     {"scope_modes", {"all", "selected"}},
                     {"password_min_length", state.policy.password_min_length},
                     {"password_max_length", 32},
                     {"login_notice", state.policy.login_notice},
                     {"scope_tree", scope_tree(*tx, call)}},
                    false);
    }
    if (path == "/api/users/security-settings") return done(public_policy(a, state.policy), false);
    if (path == "/api/profile/me") return done(dto(state.user), false);
    if (path == "/api/users/detail") return done(dto(*target), false);
    if (path == "/api/users/list") {
        auto [page, size] = identity_page(body);
        UserQuery q;
        q.offset = (page - 1) * size;
        q.limit = size;
        if (body.contains("keyword")) q.keyword = identity_text(body, "keyword", 64, true);
        if (body.contains("role_code")) {
            q.role_code = identity_text(body, "role_code", 32, true);
            if (!q.role_code.empty())
                identity_enum(body, "role_code", {"admin", "operator", "observer"});
        }
        if (body.contains("status")) {
            q.status = identity_text(body, "status", 32, true);
            if (!q.status.empty()) identity_enum(body, "status", {"enabled", "disabled"});
        }
        auto result = identity_value(a.db.users->users(*tx, q, identity_time(), call.context));
        Json items = Json::array();
        for (const auto& user : result.items) items.push_back(dto(user));
        return done({{"items", items},
                     {"total", result.matched},
                     {"page", page},
                     {"page_size", size},
                     {"stats",
                      {{"total", result.total},
                       {"enabled", result.enabled},
                       {"disabled", result.disabled},
                       {"locked", result.locked}}}},
                    false);
    }
    if (path.find("sessions/") != std::string::npos) {
        const auto uid = profile ? state.user.user_id : target->user_id;
        auto sessions = identity_value(a.db.users->sessions(*tx, uid, !read, call.context));
        if (read) {
            auto [page, size] = identity_page(body);
            Json list = Json::array();
            auto now = identity_time();
            for (const auto& s : sessions)
                if (s.absolute_expires_at > now &&
                    identity_add(s.last_interactive_at, state.policy.idle_timeout_minutes * 60) >
                        now)
                    list.push_back({{"session_id", s.session_id},
                                    {"client", client_name(s.user_agent)},
                                    {"source_ip", mask_ip(s.source_ip)},
                                    {"created_at", identity_iso(s.created_at)},
                                    {"last_interactive_at", identity_iso(s.last_interactive_at)},
                                    {"absolute_expires_at", identity_iso(s.absolute_expires_at)},
                                    {"idle_expires_at",
                                     identity_iso(std::min(
                                         s.absolute_expires_at,
                                         identity_add(s.last_interactive_at,
                                                      state.policy.idle_timeout_minutes * 60)))},
                                    {"current", s.session_id == state.session.session_id}});
            Json items = Json::array();
            for (std::size_t i = (page - 1) * size;
                 i < list.size() && items.size() < std::size_t(size); ++i)
                items.push_back(list.at(i));
            return done(
                {{"items", items}, {"total", list.size()}, {"page", page}, {"page_size", size}},
                false);
        }
        if (!profile) {
            a.recent(state);
            if (uid == state.user.user_id)
                throw ApiError(422, "InvalidArgument", "self_management_forbidden");
        }
        bool all = false;
        if (body.contains("all")) {
            if (!body.at("all").is_boolean() || !body.at("all").get<bool>())
                throw ApiError(400, "InvalidArgument", "invalid_argument");
            all = true;
        }
        if (all && body.contains("session_id"))
            throw ApiError(400, "InvalidArgument", "invalid_argument");
        auto id = all ? std::string{} : identity_text(body, "session_id", 64);
        bool found = all;
        bool current = false;
        for (auto s : sessions)
            if (all || s.session_id == id) {
                found = true;
                current |= s.session_id == state.session.session_id;
                s.revoked_at = identity_time();
                s.revoke_reason = "manual_revoke";
                identity_value(a.db.users->save_session(*tx, s, call.context));
            }
        // 已撤销 ID 同样检查归属，不能把他人会话的 ID 当作本人的幂等成功。
        if (!found) {
            // 单会话查询仓储用于包括已撤销记录的稳定幂等归属检查。
            auto prior = identity_value(a.db.users->find_session(*tx, uid, id, call.context));
            if (!prior) throw ApiError(404, "NotFound", "session_not_found");
            current = prior->session_id == state.session.session_id;
        }
        a.audit(*tx, call, state.user.user_id, uid, "session.revoke", "success", "",
                {{"all", all}, {"session_id", all ? Json(nullptr) : Json(id)}});
        return done({{"revoked", true}, {"logged_out", current}});
    }
    if (path == "/api/profile/update") {
        const auto previous_name = state.user.display_name;
        state.user.display_name = identity_text(body, "display_name", 64);
        state.user.updated_at = identity_time();
        identity_value(a.db.users->save_user(*tx, state.user, call.context));
        a.audit(
            *tx, call, state.user.user_id, state.user.user_id, "profile.update", "success", "",
            {{"display_name", {{"before", previous_name}, {"after", state.user.display_name}}}});
        return done(dto(state.user));
    }
    if (policy_change) {
        a.recent(state);
        expect_identity_edit(body, public_policy(a, state.policy));
        auto number = [&](const char* key, int minimum, int maximum) {
            if (!body.contains(key) || !body.at(key).is_number_integer())
                throw ApiError(400, "InvalidArgument", "invalid_argument");
            auto value = body.at(key).get<std::int64_t>();
            if (value < minimum || value > maximum)
                throw ApiError(400, "InvalidArgument", "invalid_argument");
            return int(value);
        };
        auto old = public_policy(a, state.policy);
        auto& p = state.policy;
        p.failed_login_limit = number("failed_login_limit", 3, 6);
        p.lock_minutes = number("lock_minutes", 10, 30);
        p.idle_timeout_minutes = number("idle_timeout_minutes", 10, 30);
        p.password_min_length = number("password_min_length", 12, 32);
        p.login_notice = identity_text(body, "login_notice", 500, true, true);
        p.updated_at = identity_time();
        identity_value(a.db.users->save_policy(*tx, p, call.context));
        auto current = public_policy(a, p);
        old.erase("edit_token");
        auto changed = current;
        changed.erase("edit_token");
        a.audit(*tx, call, state.user.user_id, "", "policy.update", "success", "",
                {{"before", old}, {"after", changed}});
        return done(current);
    }
    if (create) {
        auto role = identity_enum(body, "role_code", {"admin", "operator", "observer"});
        auto status = identity_enum(body, "status", {"enabled", "disabled"});
        if (role == "admin" || status == "enabled") a.recent(state);
        auto name = identity_text(body, "username", 64);
        validate_username(name);
        validate_password(next_password, name, state.policy.password_min_length);
        if (identity_value(a.db.users->user(*tx, name, true, IdentityLock::None, call.context)))
            throw ApiError(409, "Conflict", "username_conflict");
        auto scopes = normalize_scopes(*tx, body, call);
        UserRecord user;
        user.user_id = random_token();
        user.username = name;
        user.display_name = identity_text(body, "display_name", 64);
        user.password_hash = next_hash;
        user.role_code = role;
        user.status = status;
        user.scope_mode = body.at("scope_mode");
        user.temporary_password_expires_at = identity_time(24 * 3600);
        user.created_at = user.updated_at = identity_time();
        identity_value(a.db.users->save_user(*tx, user, call.context));
        identity_value(a.db.users->replace_scopes(*tx, user.user_id, scopes, call.context));
        a.audit(*tx, call, state.user.user_id, user.user_id, "user.create", "success", "",
                {{"role_code", user.role_code},
                 {"status", user.status},
                 {"scope_mode", user.scope_mode},
                 {"scopes", public_user(a, user, scopes).at("scopes")}});
        return done(public_user(a, user, scopes));
    }
    if (!target) throw ApiError(404, "NotFound", "user_not_found");
    if (target->user_id == state.user.user_id)
        throw ApiError(422, "InvalidArgument", "self_management_forbidden");
    auto before = dto(*target);
    expect_identity_edit(body, before);
    if (path != "/api/users/update") a.recent(state);
    bool revoke = false, remove = false;
    if (path == "/api/users/update")
        target->display_name = identity_text(body, "display_name", 64);
    else if (path == "/api/users/set-status") {
        target->status = identity_enum(body, "status", {"enabled", "disabled"});
        revoke = target->status == "disabled";
    } else if (path == "/api/users/delete") {
        target->status = "deleted";
        target->deleted_at = identity_time();
        revoke = true;
        remove = true;
    } else if (path == "/api/users/set-access") {
        target->role_code = identity_enum(body, "role_code", {"admin", "operator", "observer"});
        auto scopes = normalize_scopes(*tx, body, call);
        target->scope_mode = body.at("scope_mode");
        identity_value(a.db.users->replace_scopes(*tx, target->user_id, scopes, call.context));
        revoke = true;
    } else if (reset) {
        if (target->password_hash != original_hash)
            throw ApiError(409, "Conflict", "edit_conflict");
        validate_password(next_password, target->username, state.policy.password_min_length);
        target->password_hash = next_hash;
        target->must_change_password = 1;
        target->temporary_password_expires_at = identity_time(24 * 3600);
        target->password_changed_at = identity_time();
        target->failed_attempts = 0;
        target->locked_until.reset();
        revoke = true;
    } else if (path == "/api/users/unlock") {
        target->failed_attempts = 0;
        target->locked_until.reset();
    } else
        throw ApiError(404, "NotFound", "route_not_found");
    bool was_admin = before.at("role_code") == "admin" && before.at("status") == "enabled";
    if (was_admin && (target->role_code != "admin" || target->status != "enabled") &&
        identity_value(a.db.users->count_users(*tx, true, call.context)) <= 1)
        throw ApiError(409, "Conflict", "last_admin_required");
    target->updated_at = identity_time();
    identity_value(a.db.users->save_user(*tx, *target, call.context));
    if (revoke) a.revoke_all(*tx, target->user_id, "user_changed", call);
    if (remove) identity_value(a.db.users->replace_scopes(*tx, target->user_id, {}, call.context));
    auto result = dto(*target);
    Json differences;
    for (auto key : {"display_name", "role_code", "status", "scope_mode", "scopes"})
        if (before.at(key) != result.at(key))
            differences[key] = {{"before", before.at(key)}, {"after", result.at(key)}};
    if (differences.contains("scopes")) {
        // 200 个根的前后值用列名加二元组编码，避免重复键名撑大审计 JSON。
        auto compact = [](const Json& scopes) {
            Json pairs = Json::array();
            for (const auto& scope : scopes)
                pairs.push_back(Json::array({scope.at("station_id"), scope.at("device_root_id")}));
            return pairs;
        };
        differences["scopes"] = {{"columns", {"station_id", "device_root_id"}},
                                 {"before", compact(before.at("scopes"))},
                                 {"after", compact(result.at("scopes"))}};
    }
    auto action = path.substr(path.find_last_of('/') + 1);
    std::replace(action.begin(), action.end(), '-', '_');
    a.audit(*tx, call, state.user.user_id, target->user_id, "user." + action, "success", "",
            differences);
    if (remove) return done({{"removed_id", target->user_id}});
    return done(result);
}

Json UserService::provision(const std::string& username, const std::string& display_name,
                            const std::string& password, bool recover, const IdentityCall& call) {
    auto& a = *auth_;
    validate_username(username);
    identity_text({{"name", display_name}}, "name", 64);
    auto read = identity_value(a.db.database->begin(call.context));
    auto p = identity_value(a.db.users->policy(*read, false, call.context));
    identity_value(read->rollback(call.context));
    validate_password(password, username, p.password_min_length);
    auto hash = hash_password(password);
    auto tx = identity_value(a.db.database->begin(call.context));
    p = identity_value(a.db.users->policy(*tx, true, call.context));
    validate_password(password, username, p.password_min_length);
    auto existing =
        identity_value(a.db.users->user(*tx, username, true, IdentityLock::Update, call.context));
    UserRecord user;
    if (recover) {
        if (!existing || existing->status == "deleted" || existing->role_code != "admin")
            throw ApiError(404, "NotFound", "user_not_found");
        user = *existing;
    } else {
        if (identity_value(a.db.users->count_users(*tx, false, call.context)) != 0)
            throw ApiError(409, "Conflict", "already_initialized");
        user.user_id = random_token();
        user.username = username;
        user.display_name = display_name;
        user.role_code = "admin";
        user.scope_mode = "all";
        user.created_at = identity_time();
    }
    user.status = "enabled";
    user.password_hash = hash;
    user.must_change_password = 1;
    user.temporary_password_expires_at = identity_time(24 * 3600);
    user.failed_attempts = 0;
    user.locked_until.reset();
    user.updated_at = identity_time();
    user.password_changed_at = identity_time();
    identity_value(a.db.users->save_user(*tx, user, call.context));
    a.revoke_all(*tx, user.user_id, "host_recovery", call);
    a.audit(*tx, call, "", user.user_id, recover ? "host.recover_admin" : "host.initialize_admin");
    auto result =
        public_user(a, user, identity_value(a.db.users->scopes(*tx, user.user_id, call.context)));
    identity_value(tx->commit(call.context));
    return result;
}

void UserService::maintain(const IdentityCall& call, int audit_retention_days) {
    if (audit_retention_days < 1 || audit_retention_days > 3650)
        throw std::invalid_argument("audit retention");
    auto& a = *auth_;
    auto tx = identity_value(a.db.database->begin(call.context));
    identity_value(a.db.users->prune_sessions(*tx, identity_time(-24 * 3600), call.context));
    identity_value(
        a.db.audit->prune(*tx, identity_time(-audit_retention_days * 24 * 3600), call.context));
    identity_value(tx->commit(call.context));
}
}  // namespace ems::web
