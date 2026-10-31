#include "identity_codec.h"

#include <algorithm>
#include <cctype>

#include "web/platform/crypto.h"

namespace ems::web {
void validate_identity_request(const std::string& path, const Json& body) {
    if (path == "/api/users/capabilities" || path == "/api/users/security-settings" ||
        path == "/api/profile/me")
        fields(body, {});
    else if (path == "/api/users/list")
        fields(body, {"keyword", "role_code", "status", "page", "page_size"});
    else if (path == "/api/users/detail")
        fields(body, {"user_id"});
    else if (path == "/api/users/create")
        fields(body, {"username", "display_name", "role_code", "status", "scope_mode", "scopes",
                      "temporary_password"});
    else if (path == "/api/users/update")
        fields(body, {"user_id", "display_name", "expected_edit_token"});
    else if (path == "/api/users/set-status")
        fields(body, {"user_id", "status", "expected_edit_token"});
    else if (path == "/api/users/set-access")
        fields(body, {"user_id", "role_code", "scope_mode", "scopes", "expected_edit_token"});
    else if (path == "/api/users/reset-password")
        fields(body, {"user_id", "temporary_password", "expected_edit_token"});
    else if (path == "/api/users/unlock" || path == "/api/users/delete")
        fields(body, {"user_id", "expected_edit_token"});
    else if (path == "/api/users/security-settings/update")
        fields(body, {"failed_login_limit", "lock_minutes", "idle_timeout_minutes",
                      "password_min_length", "login_notice", "expected_edit_token"});
    else if (path == "/api/users/sessions/list")
        fields(body, {"user_id", "page", "page_size"});
    else if (path == "/api/users/sessions/revoke")
        fields(body, {"user_id", "session_id", "all"});
    else if (path == "/api/profile/update")
        fields(body, {"display_name"});
    else if (path == "/api/profile/sessions/list")
        fields(body, {"page", "page_size"});
    else if (path == "/api/profile/sessions/revoke")
        fields(body, {"session_id"});
    else
        throw ApiError(404, "NotFound", "route_not_found");
}

std::string identity_text(const Json& body, const char* key, std::size_t maximum, bool allow_empty,
                          bool allow_line_breaks) {
    if (!body.contains(key) || !body.at(key).is_string())
        throw ApiError(400, "InvalidArgument", "invalid_argument");
    auto text = body.at(key).get<std::string>();
    std::size_t count = 0;
    if (text.size() > maximum * 4 || (!allow_empty && text.empty()))
        throw ApiError(400, "InvalidArgument", "invalid_argument");
    for (std::size_t i = 0; i < text.size();) {
        unsigned char c = text[i++];
        unsigned value = 0;
        int remaining = 0;
        unsigned minimum = 0;
        if (c < 0x80) {
            value = c;
        } else if (c >= 0xc2 && c <= 0xdf) {
            value = c & 31;
            remaining = 1;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            value = c & 15;
            remaining = 2;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            value = c & 7;
            remaining = 3;
            minimum = 0x10000;
        } else
            throw ApiError(400, "InvalidArgument", "invalid_argument");
        while (remaining--) {
            if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                throw ApiError(400, "InvalidArgument", "invalid_argument");
            value = (value << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (value < 32 && !(allow_line_breaks && (value == 9 || value == 10 || value == 13))) ||
            value == 127)
            throw ApiError(400, "InvalidArgument", "invalid_argument");
        if (++count > maximum) throw ApiError(400, "InvalidArgument", "invalid_argument");
    }
    if (!allow_empty && text.find_first_not_of(' ') == std::string::npos)
        throw ApiError(400, "InvalidArgument", "invalid_argument");
    return text;
}

void validate_username(const std::string& name) {
    if (name.size() < 4 || name.size() > 64 ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                   c == '-';
        }))
        throw ApiError(400, "InvalidArgument", "invalid_argument");
}

void validate_password(const std::string& password, const std::string& username, int minimum) {
    bool upper = false, lower = false, digit = false, special = false;
    std::size_t length = 0;
    try {
        identity_text({{"password", password}}, "password", 32);
    } catch (const ApiError&) {
        throw ApiError(422, "InvalidArgument", "password_policy_failed");
    }
    for (unsigned char c : password) {
        if ((c & 0xc0) != 0x80) ++length;
        upper |= c >= 'A' && c <= 'Z';
        lower |= c >= 'a' && c <= 'z';
        digit |= c >= '0' && c <= '9';
        special |= !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9');
    }
    auto folded = password;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    static const std::set<std::string> weak{"password123!",    "password1234!", "admin123456!",
                                            "administrator1!", "qwerty123456!", "changeme123!",
                                            "welcome12345!"};
    if (length < static_cast<std::size_t>(minimum) || upper + lower + digit + special < 3 ||
        folded == username || weak.count(folded) || folded.rfind("password", 0) == 0 ||
        folded.rfind("qwerty", 0) == 0 || folded.rfind("123456", 0) == 0)
        throw ApiError(422, "InvalidArgument", "password_policy_failed");
}

std::string identity_enum(const Json& body, const char* key,
                          std::initializer_list<const char*> choices) {
    auto value = identity_text(body, key, 32);
    for (auto choice : choices)
        if (value == choice) return value;
    throw ApiError(400, "InvalidArgument", "invalid_argument");
}

std::pair<int, int> identity_page(const Json& body) {
    for (auto key : {"page", "page_size"})
        if (body.contains(key) && !body.at(key).is_number_integer())
            throw ApiError(400, "InvalidArgument", "invalid_argument");
    auto page = body.value("page", std::int64_t{1}),
         size = body.value("page_size", std::int64_t{20});
    if (page < 1 || page > 10000 || size < 1 || size > 200)
        throw ApiError(400, "InvalidArgument", "invalid_argument");
    return {int(page), int(size)};
}

Json public_user(const AuthorizationService& auth, const database::UserRecord& user,
                 const std::vector<database::ScopeRecord>& source) {
    auto scopes = source;
    std::sort(scopes.begin(), scopes.end(), [](const auto& a, const auto& b) {
        return std::tie(a.station_id, a.device_root_id) < std::tie(b.station_id, b.device_root_id);
    });
    Json list = Json::array();
    for (const auto& s : scopes)
        list.push_back({{"station_id", s.station_id},
                        {"device_root_id",
                         s.device_root_id.empty() ? Json(nullptr) : Json(s.device_root_id)}});
    Json result = {{"user_id", user.user_id},
                   {"username", user.username},
                   {"display_name", user.display_name},
                   {"role_code", user.role_code},
                   {"status", user.status},
                   {"scope_mode", user.scope_mode},
                   {"scopes", list},
                   {"must_change_password", bool(user.must_change_password)},
                   {"locked_until", identity_iso(user.locked_until)},
                   {"password_changed_at", identity_iso(user.password_changed_at)},
                   {"created_at", identity_iso(user.created_at)},
                   {"updated_at", identity_iso(user.updated_at)},
                   {"last_login_at", identity_iso(user.last_login_at)}};
    Json editable = {
        {"user_id", user.user_id},
        {"display_name", user.display_name},
        {"role_code", user.role_code},
        {"status", user.status},
        {"scope_mode", user.scope_mode},
        {"scopes", list},
        {"must_change_password", user.must_change_password},
        {"temporary_password_expires_at", identity_iso(user.temporary_password_expires_at)},
        {"credential", sha256(user.password_hash)}};
    result["edit_token"] = hmac_sha256(auth.edit_key, "user:" + editable.dump());
    return result;
}

Json public_policy(const AuthorizationService& auth, const database::LoginPolicyRecord& p) {
    Json j = {{"failed_login_limit", p.failed_login_limit},
              {"lock_minutes", p.lock_minutes},
              {"idle_timeout_minutes", p.idle_timeout_minutes},
              {"password_min_length", p.password_min_length},
              {"login_notice", p.login_notice}};
    j["edit_token"] = hmac_sha256(auth.edit_key, "policy:1:" + j.dump());
    return j;
}

void expect_identity_edit(const Json& body, const Json& current) {
    if (!secure_equal(identity_text(body, "expected_edit_token", 64),
                      current.at("edit_token").get<std::string>()))
        throw ApiError(409, "Conflict", "edit_conflict");
}
}  // namespace ems::web
