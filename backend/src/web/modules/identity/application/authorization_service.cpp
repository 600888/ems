#include "authorization_service.h"

#include <ctime>
#include <iomanip>
#include <sstream>

#include "web/platform/crypto.h"

namespace ems::web {
using namespace database;

namespace {
std::string format(std::chrono::system_clock::time_point time) {
    auto raw = std::chrono::system_clock::to_time_t(time);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() %
        1000;
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << ms;
    return out.str();
}
}  // namespace

std::string identity_time(int seconds) {
    return format(std::chrono::system_clock::now() + std::chrono::seconds(seconds));
}

std::string identity_add(const std::string& time, int seconds) {
    std::tm utc{};
    std::istringstream in(time);
    in >> std::get_time(&utc, "%Y-%m-%d %H:%M:%S");
    if (in.fail()) throw ApiError(503, "CorruptData", "identity_unavailable");
#ifdef _WIN32
    auto raw = _mkgmtime(&utc);
#else
    auto raw = timegm(&utc);
#endif
    int ms = time.size() >= 23 ? std::stoi(time.substr(20, 3)) : 0;
    return format(std::chrono::system_clock::from_time_t(raw) + std::chrono::seconds(seconds) +
                  std::chrono::milliseconds(ms));
}

Json identity_iso(const std::optional<std::string>& time) {
    if (!time) return nullptr;
    auto value = *time;
    if (value.size() < 19) throw ApiError(503, "CorruptData", "identity_unavailable");
    value[10] = 'T';
    return value + 'Z';
}

AuthorizationService::AuthorizationService(MySqlDatabaseContext database, std::string key)
    : db(std::move(database)), edit_key(std::move(key)) {
    if (!db.database || !db.users || !db.audit || edit_key.size() < 32 || edit_key.size() > 4096)
        throw std::invalid_argument("identity dependencies");
}

void AuthorizationService::check_schema(const core::CallContext& ctx) {
    auto tx = identity_value(db.database->begin(ctx));
    identity_value(db.users->policy(*tx, false, ctx));
    identity_value(db.users->user(*tx, "schema-check", false, IdentityLock::None, ctx));
    identity_value(db.users->scopes(*tx, "schema-check", ctx));
    identity_value(db.users->sessions(*tx, "schema-check", false, ctx));
    identity_value(db.audit->list(*tx, 0, 1, ctx));
    identity_value(tx->rollback(ctx));
}

std::set<std::string> AuthorizationService::permissions(const std::string& role) {
    if (role != "admin" && role != "operator" && role != "observer")
        throw ApiError(503, "CorruptData", "identity_unavailable");
    std::set<std::string> result{"monitor.read", "history.read", "alarm.read", "device.read"};
    if (role != "observer")
        result.insert({"alarm.ack", "command.yk", "command.yt", "channel.read"});
    if (role == "admin")
        result.insert({"device.write", "channel.write", "binding.write", "point.write",
                       "import.write", "user.read", "user.write", "user.grant", "user.security",
                       "session.manage", "audit.read", "system.read"});
    return result;
}

Principal AuthorizationService::principal(const AuthState& state) {
    Principal p;
    p.subject_id = state.user.user_id;
    p.session_id = state.session.session_id;
    p.kind = state.session.kind;
    p.role_code = state.user.role_code;
    p.scope_mode = state.user.scope_mode;
    if (p.kind != "full") return p;
    p.permissions = permissions(p.role_code);
    if (p.scope_mode == "all") {
        if (p.role_code != "admin") throw ApiError(503, "CorruptData", "identity_unavailable");
        p.station_ids.insert("*");
        p.device_roots.insert("*");
    } else if (p.scope_mode == "selected") {
        for (const auto& scope : state.scopes) {
            p.scopes.push_back({scope.station_id, scope.device_root_id});
            p.station_ids.insert(scope.station_id);
            p.device_roots.insert(scope.device_root_id.empty() ? scope.station_id
                                                               : scope.device_root_id);
        }
    } else
        throw ApiError(503, "CorruptData", "identity_unavailable");
    return p;
}

void AuthorizationService::valid(const AuthState& state) {
    auto now = identity_time();
    if (state.user.status != "enabled" || state.session.revoked_at ||
        state.session.absolute_expires_at <= now ||
        identity_add(state.session.last_interactive_at, state.policy.idle_timeout_minutes * 60) <=
            now ||
        (state.session.kind != "full" && state.session.kind != "password_change") ||
        (state.session.kind == "full" && state.user.must_change_password) ||
        (state.session.kind == "password_change" &&
         (!state.user.must_change_password || !state.user.temporary_password_expires_at ||
          *state.user.temporary_password_expires_at <= now)))
        throw ApiError(401, "Unauthenticated", "unauthenticated");
}

void AuthorizationService::full(const AuthState& state) {
    if (state.session.kind != "full")
        throw ApiError(403, "PermissionDenied", "password_change_required");
}

void AuthorizationService::recent(const AuthState& state) {
    full(state);
    if (identity_add(state.session.auth_time, 300) < identity_time())
        throw ApiError(403, "PermissionDenied", "reauthentication_required");
}

AuthState AuthorizationService::authenticate(ITransaction& tx, const std::string& token,
                                             IdentityLock lock, const IdentityCall& call) {
    if (token.size() != 64 || token.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw ApiError(401, "Unauthenticated", "unauthenticated");
    auto s = identity_value(db.users->session(tx, digest_bytes(sha256(token)), call.context));
    if (!s) throw ApiError(401, "Unauthenticated", "unauthenticated");
    AuthState state;
    state.policy = identity_value(db.users->policy(tx, false, call.context));
    auto u = identity_value(db.users->user(tx, s->user_id, false, lock, call.context));
    if (!u) throw ApiError(401, "Unauthenticated", "unauthenticated");
    state.user = *u;
    auto sessions = identity_value(
        db.users->sessions(tx, u->user_id, lock == IdentityLock::Update, call.context));
    bool found = false;
    for (const auto& current : sessions)
        if (current.session_id == s->session_id) {
            state.session = current;
            found = true;
            break;
        }
    if (!found) throw ApiError(401, "Unauthenticated", "unauthenticated");
    state.scopes = identity_value(db.users->scopes(tx, u->user_id, call.context));
    valid(state);
    state.principal = principal(state);
    return state;
}

Principal AuthorizationService::recheck(MySqlDatabaseContext& db, ITransaction& tx,
                                        const Principal& actor, const core::CallContext& ctx) {
    if (actor.session_id.empty()) return actor;
    AuthState state;
    state.policy = identity_value(db.users->policy(tx, false, ctx));
    auto u = identity_value(db.users->user(tx, actor.subject_id, false, IdentityLock::Share, ctx));
    if (!u) throw ApiError(401, "Unauthenticated", "unauthenticated");
    state.user = *u;
    bool found = false;
    for (const auto& s : identity_value(db.users->sessions(tx, u->user_id, false, ctx)))
        if (s.session_id == actor.session_id) {
            state.session = s;
            found = true;
            break;
        }
    if (!found) throw ApiError(401, "Unauthenticated", "unauthenticated");
    state.scopes = identity_value(db.users->scopes(tx, u->user_id, ctx));
    valid(state);
    full(state);
    return principal(state);
}

Json AuthorizationService::session_json(const AuthState& state) const {
    Json result = {{"kind", state.session.kind},
                   {"next_action", state.session.kind == "full" ? "enter_app" : "change_password"},
                   {"user",
                    {{"user_id", state.user.user_id},
                     {"username", state.user.username},
                     {"display_name", state.user.display_name},
                     {"role_code", state.user.role_code}}},
                   {"csrf_token", state.session.csrf_token},
                   {"idle_expires_at",
                    identity_iso(std::min(state.session.absolute_expires_at,
                                          identity_add(state.session.last_interactive_at,
                                                       state.policy.idle_timeout_minutes * 60)))},
                   {"absolute_expires_at", identity_iso(state.session.absolute_expires_at)},
                   {"password_min_length", state.policy.password_min_length}};
    if (state.session.kind == "full") {
        auto p = principal(state);
        Json scopes = Json::array();
        for (const auto& scope : state.scopes)
            scopes.push_back(
                {{"station_id", scope.station_id},
                 {"device_root_id",
                  scope.device_root_id.empty() ? Json(nullptr) : Json(scope.device_root_id)}});
        result["permissions"] = p.permissions;
        result["scopes"] = scopes;
        result["scope_mode"] = p.scope_mode;
        result["auth_time"] = identity_iso(state.session.auth_time);
    }
    return result;
}

void AuthorizationService::audit(ITransaction& tx, const IdentityCall& call,
                                 const std::string& actor, const std::string& target,
                                 const std::string& action, const std::string& outcome,
                                 const std::string& reason, const Json& details) {
    AuditRecord event;
    event.event_id = random_token();
    if (!actor.empty()) event.actor_user_id = actor;
    if (!target.empty()) event.target_user_id = target;
    event.action = action;
    event.resource_type = "identity";
    if (!target.empty()) event.resource_id = target;
    event.outcome = outcome;
    if (!reason.empty()) event.reason = reason;
    event.request_id = call.context.trace_id.empty() ? random_token() : call.context.trace_id;
    if (!call.source_ip.empty()) event.source_ip = call.source_ip;
    event.details = details.dump();
    event.occurred_at = identity_time();
    auto result = db.audit->append(tx, event, call.context);
    if (!core::is_ok(result)) {
        auto e = core::error_of(result);
        if (e.effect == core::WriteEffect::Unknown) fail(e);
        throw ApiError(503, "Unavailable", "audit_unavailable");
    }
}

void AuthorizationService::revoke_all(ITransaction& tx, const std::string& user_id,
                                      const std::string& reason, const IdentityCall& call) {
    for (auto s : identity_value(db.users->sessions(tx, user_id, true, call.context))) {
        s.revoked_at = identity_time();
        s.revoke_reason = reason;
        identity_value(db.users->save_session(tx, s, call.context));
    }
}
}  // namespace ems::web
