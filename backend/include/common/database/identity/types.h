#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ems::database {
struct UserRecord {
    std::int64_t id{0};
    std::string user_id;
    std::string username;
    std::string display_name;
    std::string password_hash;
    std::string role_code{"observer"};
    std::string status{"disabled"};
    std::string scope_mode{"selected"};
    int must_change_password{1};
    std::optional<std::string> temporary_password_expires_at;
    int failed_attempts{0};
    std::optional<std::string> locked_until;
    std::optional<std::string> password_changed_at;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> last_login_at;
    std::optional<std::string> deleted_at;
};

struct ScopeRecord {
    std::int64_t id{0};
    std::string user_id;
    std::string station_id;
    std::string device_root_id;
};

struct SessionRecord {
    std::int64_t id{0};
    std::string session_id;
    std::string user_id;
    std::string kind;
    std::vector<char> token_hash;
    std::string csrf_token;
    std::string created_at;
    std::string auth_time;
    std::string last_interactive_at;
    std::string absolute_expires_at;
    std::optional<std::string> revoked_at;
    std::optional<std::string> revoke_reason;
    std::optional<std::string> source_ip;
    std::string user_agent;
};

struct LoginPolicyRecord {
    int id{1};
    int failed_login_limit{6};
    int lock_minutes{10};
    int idle_timeout_minutes{10};
    int password_min_length{12};
    std::string login_notice;
    std::string updated_at;
};

struct AuditRecord {
    std::int64_t id{0};
    std::string event_id;
    std::optional<std::string> actor_user_id;
    std::optional<std::string> target_user_id;
    std::string action;
    std::string resource_type;
    std::optional<std::string> resource_id;
    std::string outcome;
    std::optional<std::string> reason;
    std::string request_id;
    std::optional<std::string> source_ip;
    std::optional<std::string> details;
    std::string occurred_at;
};

struct UserQuery {
    std::string keyword, role_code, status;
    std::size_t offset{0}, limit{20};
};

struct UserPage {
    std::vector<UserRecord> items;
    std::int64_t total{0}, enabled{0}, disabled{0}, locked{0}, matched{0};
};
}  // namespace ems::database
