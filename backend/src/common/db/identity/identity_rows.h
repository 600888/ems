#pragma once
#include <common/database/identity/types.h>

#include <iguana/ylt/reflection/user_reflect_macro.hpp>
#include <ormpp/utility.hpp>

namespace ems::database {
/** @brief 固定跨库映射。 @return 库限定表名。 */
constexpr std::string_view get_alias_struct_name(UserRecord*) { return "ems_web`.`auth_user"; }
YLT_REFL(UserRecord, id, user_id, username, display_name, password_hash, role_code, status,
         scope_mode, must_change_password, temporary_password_expires_at, failed_attempts,
         locked_until, password_changed_at, created_at, updated_at, last_login_at, deleted_at)
inline const auto UserRecord_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<UserRecord>(), "id");
inline const auto UserRecord_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<UserRecord>(), "id");

/** @brief 固定跨库映射。 @return 库限定表名。 */
constexpr std::string_view get_alias_struct_name(ScopeRecord*) {
    return "ems_web`.`auth_user_scope";
}
YLT_REFL(ScopeRecord, id, user_id, station_id, device_root_id)
inline const auto ScopeRecord_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<ScopeRecord>(), "id");
inline const auto ScopeRecord_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<ScopeRecord>(), "id");

/** @brief 固定跨库映射。 @return 库限定表名。 */
constexpr std::string_view get_alias_struct_name(SessionRecord*) {
    return "ems_web`.`auth_session";
}
YLT_REFL(SessionRecord, id, session_id, user_id, kind, token_hash, csrf_token, created_at,
         auth_time, last_interactive_at, absolute_expires_at, revoked_at, revoke_reason, source_ip,
         user_agent)
inline const auto SessionRecord_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<SessionRecord>(), "id");
inline const auto SessionRecord_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<SessionRecord>(), "id");

/** @brief 固定跨库映射。 @return 库限定表名。 */
constexpr std::string_view get_alias_struct_name(LoginPolicyRecord*) {
    return "ems_web`.`auth_login_policy";
}
YLT_REFL(LoginPolicyRecord, id, failed_login_limit, lock_minutes, idle_timeout_minutes,
         password_min_length, login_notice, updated_at)
inline const auto LoginPolicyRecord_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<LoginPolicyRecord>(), "id");

/** @brief 固定跨库映射。 @return 库限定表名。 */
constexpr std::string_view get_alias_struct_name(AuditRecord*) { return "ems_web`.`audit_log"; }
YLT_REFL(AuditRecord, id, event_id, actor_user_id, target_user_id, action, resource_type,
         resource_id, outcome, reason, request_id, source_ip, details, occurred_at)
inline const auto AuditRecord_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<AuditRecord>(), "id");
inline const auto AuditRecord_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<AuditRecord>(), "id");
}  // namespace ems::database
