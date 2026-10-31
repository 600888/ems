#pragma once
#include <common/database/mysql_database.h>

#include "web/modules/identity/public/authorization.h"

namespace ems::web {
/** @brief 将身份依赖错误转换为稳定错误，保留写入确定性。
 * @param[in] result 公共仓储结果。 @return 成功值；损坏或缺失身份结构返回 503。
 */
template <class T>
T identity_value(core::Result<T> result) {
    if (!core::is_ok(result)) {
        const auto& error = core::error_of(result);
        if (error.code == core::ErrorCode::SchemaMismatch ||
            error.code == core::ErrorCode::CorruptData || error.code == core::ErrorCode::Internal ||
            error.code == core::ErrorCode::Unavailable || error.code == core::ErrorCode::NotReady)
            throw ApiError(503, "Unavailable", "identity_unavailable",
                           core::to_string(error.effect));
        fail(error);
    }
    return std::move(std::get<0>(result));
}

struct IdentityCall {
    core::CallContext context = core::make_call_context(std::chrono::seconds(30));
    std::string source_ip, user_agent;
};

struct AuthState {
    database::UserRecord user;
    database::SessionRecord session;
    database::LoginPolicyRecord policy;
    std::vector<database::ScopeRecord> scopes;
    Principal principal;
};

class AuthorizationService {
   public:
    database::MySqlDatabaseContext db;
    std::string edit_key;
    /** @brief 装配公共仓储与跨实例摘要密钥。
     * @param[in] database 共享上下文。 @param[in] key 至少 32 字节密钥。
     */
    AuthorizationService(database::MySqlDatabaseContext database, std::string key);
    /** @brief 验证五表和唯一策略行，不自动建表。
     * @param[in] ctx 期限。
     */
    void check_schema(const core::CallContext& ctx);
    /** @brief 在用户锁保护下恢复当前会话；不续期。
     * @param[in,out] tx 公共事务。 @param[in] token 原始 Cookie。
     * @param[in] lock 用户锁模式。 @param[in] call 期限和来源。
     * @return 当前状态，失败抛 ApiError。
     */
    AuthState authenticate(database::ITransaction& tx, const std::string& token,
                           database::IdentityLock lock, const IdentityCall& call);
    /** @brief 在已有事务内按主体重新授权，先锁用户再读会话。
     * @param[in,out] db 公共数据库上下文。 @param[in,out] tx 调用方事务。 @param[in] actor 请求主体。
     * @param[in] ctx 期限。 @return 最新主体；测试主体无 session_id 时由调用方控制注入。
     */
    static Principal recheck(database::MySqlDatabaseContext& db, database::ITransaction& tx,
                             const Principal& actor, const core::CallContext& ctx);
    /** @brief 生成公开会话 DTO。 @param[in] state 当前身份。 @return 脱敏对象。 */
    Json session_json(const AuthState& state) const;
    /** @brief 当前固定角色映射。 @param[in] role 角色。 @return 明确权限集合。 */
    static std::set<std::string> permissions(const std::string& role);
    /** @brief 生成公开主体。 @param[in] state 当前行及范围。 @return 主体。 */
    static Principal principal(const AuthState& state);
    /** @brief 检查会话和账号时效。 @param[in] state 当前状态。 */
    static void valid(const AuthState& state);
    /** @brief 需要完整会话。 @param[in] state 当前身份。 */
    static void full(const AuthState& state);
    /** @brief 需要五分钟内的密码认证。 @param[in] state 当前身份。 */
    static void recent(const AuthState& state);
    /** @brief 追加脱敏事件；审计失败拒绝业务。
     * @param[in,out] tx 业务事务。 @param[in] call 调用信息。 @param[in] actor 主体 ID。
     * @param[in] target 目标 ID。 @param[in] action 固定动作。
     * @param[in] outcome success/failure。 @param[in] reason 固定原因。
     * @param[in] details 脱敏差异。
     */
    void audit(database::ITransaction& tx, const IdentityCall& call, const std::string& actor,
               const std::string& target, const std::string& action,
               const std::string& outcome = "success", const std::string& reason = "",
               const Json& details = Json::object());
    /** @brief 撤销目标全部未撤销会话。
     * @param[in,out] tx 事务。 @param[in] user_id 已锁用户。 @param[in] reason 固定原因。
     * @param[in] call 期限及来源。
     */
    void revoke_all(database::ITransaction& tx, const std::string& user_id,
                    const std::string& reason, const IdentityCall& call);
};

/** @brief 当前 UTC SQL 文本，可加相对秒。
 * @param[in] seconds 相对秒数。 @return 毫秒精度时间。
 */
std::string identity_time(int seconds = 0);
/** @brief SQL UTC 时间增加秒数。
 * @param[in] time SQL UTC 文本。 @param[in] seconds 相对秒数。 @return SQL 时间。
 */
std::string identity_add(const std::string& time, int seconds);
/** @brief SQL UTC 转公开 RFC3339。 @param[in] time SQL 文本。 @return UTC 时间。 */
Json identity_iso(const std::optional<std::string>& time);
}  // namespace ems::web
