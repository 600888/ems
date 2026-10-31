#pragma once
#include <common/database/i_database.h>
#include <common/database/identity/types.h>

namespace ems::database {
enum class IdentityLock { None, Share, Update };

/** @brief 身份数据的类型化访问；所有写入由调用方事务提交。 */
class IUserRepository {
   public:
    /** @brief 释放仓储。 */
    virtual ~IUserRepository() = default;
    /** @brief 读取唯一策略，缺失返回 SchemaMismatch。
     * @param[in,out] tx 公共事务。 @param[in] lock 是否串行管理员变更。
     * @param[in] ctx 期限。 @return 策略或错误。
     */
    virtual core::Result<LoginPolicyRecord> policy(ITransaction& tx, bool lock,
                                                   const core::CallContext& ctx) = 0;
    /** @brief 暂存策略。
     * @param[in,out] tx 公共事务。 @param[in] row 策略。 @param[in] ctx 期限。
     * @return 写入状态，不自行提交。
     */
    virtual core::Status save_policy(ITransaction& tx, const LoginPolicyRecord& row,
                                     const core::CallContext& ctx) = 0;
    /** @brief 读取用户；包括软删除用户以保留唯一名。
     * @param[in,out] tx 公共事务。 @param[in] key 用户名或稳定 ID。
     * @param[in] by_name 是否按用户名定位。 @param[in] lock 锁模式。
     * @param[in] ctx 期限。 @return 可空用户或依赖错误。
     */
    virtual core::Result<std::optional<UserRecord>> user(ITransaction& tx, const std::string& key,
                                                         bool by_name, IdentityLock lock,
                                                         const core::CallContext& ctx) = 0;
    /** @brief 有界分页和未筛选的总体统计。
     * @param[in,out] tx 公共事务。 @param[in] query 筛选分页。
     * @param[in] now 当前 UTC SQL 时间。 @param[in] ctx 期限。
     * @return 列表和计数。
     */
    virtual core::Result<UserPage> users(ITransaction& tx, const UserQuery& query,
                                         const std::string& now, const core::CallContext& ctx) = 0;
    /** @brief 统计用户或启用管理员；管理调用应先锁策略。
     * @param[in,out] tx 公共事务。 @param[in] admins true 仅计启用管理员，否则计含删除所有用户。
     * @param[in] ctx 期限。 @return 数量。
     */
    virtual core::Result<std::int64_t> count_users(ITransaction& tx, bool admins,
                                                   const core::CallContext& ctx) = 0;
    /** @brief 新增或更新内部用户行。
     * @param[in,out] tx 公共事务。 @param[in] row 用户；id=0 表示新增。
     * @param[in] ctx 期限。 @return 写入状态。
     */
    virtual core::Status save_user(ITransaction& tx, const UserRecord& row,
                                   const core::CallContext& ctx) = 0;
    /** @brief 读取规范化范围，超过 200 条拒绝。
     * @param[in,out] tx 公共事务。 @param[in] user_id 用户。 @param[in] ctx 期限。
     * @return 范围。
     */
    virtual core::Result<std::vector<ScopeRecord>> scopes(ITransaction& tx,
                                                          const std::string& user_id,
                                                          const core::CallContext& ctx) = 0;
    /** @brief 原子替换范围；用户和工程根须已锁。
     * @param[in,out] tx 公共事务。 @param[in] user_id 用户。 @param[in] rows 全部规范化范围。
     * @param[in] ctx 期限。 @return 状态。
     */
    virtual core::Status replace_scopes(ITransaction& tx, const std::string& user_id,
                                        const std::vector<ScopeRecord>& rows,
                                        const core::CallContext& ctx) = 0;
    /** @brief 按令牌摘要定位会话，不改变活动时间。
     * @param[in,out] tx 公共事务。 @param[in] hash 原始 SHA-256 32 字节。
     * @param[in] ctx 期限。 @return 会话或空值。
     */
    virtual core::Result<std::optional<SessionRecord>> session(ITransaction& tx,
                                                               const std::vector<char>& hash,
                                                               const core::CallContext& ctx) = 0;
    /** @brief 读取用户的未撤销会话；最多 1000 条，超限拒绝。
     * @param[in,out] tx 公共事务。 @param[in] user_id 用户。 @param[in] lock 是否写锁。
     * @param[in] ctx 期限。 @return 会话。
     */
    virtual core::Result<std::vector<SessionRecord>> sessions(ITransaction& tx,
                                                              const std::string& user_id, bool lock,
                                                              const core::CallContext& ctx) = 0;
    /** @brief 按归属定位会话，包括已撤销会话以支持幂等。
     * @param[in,out] tx 事务。 @param[in] user_id 用户。 @param[in] session_id 会话。
     * @param[in] ctx 期限。 @return 可空会话。
     */
    virtual core::Result<std::optional<SessionRecord>> find_session(
        ITransaction& tx, const std::string& user_id, const std::string& session_id,
        const core::CallContext& ctx) = 0;
    /** @brief 新增或更新会话。
     * @param[in,out] tx 公共事务。 @param[in] row 会话，id=0 表示新增。
     * @param[in] ctx 期限。 @return 状态。
     */
    virtual core::Status save_session(ITransaction& tx, const SessionRecord& row,
                                      const core::CallContext& ctx) = 0;
    /** @brief 分批清理绝对到期后保留至少 24 小时的会话。
     * @param[in,out] tx 公共事务。 @param[in] before 过期保留界限 UTC。
     * @param[in] ctx 期限。 @return 状态，每批最多 200 条。
     */
    virtual core::Status prune_sessions(ITransaction& tx, const std::string& before,
                                        const core::CallContext& ctx) = 0;
};

class IAuditRepository {
   public:
    /** @brief 释放仓储。 */
    virtual ~IAuditRepository() = default;
    /** @brief 追加脱敏事件，与业务处于同一事务。
     * @param[in,out] tx 公共事务。 @param[in] row 事件。 @param[in] ctx 期限。
     * @return 状态。
     */
    virtual core::Status append(ITransaction& tx, const AuditRecord& row,
                                const core::CallContext& ctx) = 0;
    /** @brief 有界读取审计。
     * @param[in,out] tx 公共事务。 @param[in] offset 偏移。 @param[in] limit 上限 200。
     * @param[in] ctx 期限。 @return 事件。
     */
    virtual core::Result<std::vector<AuditRecord>> list(ITransaction& tx, std::size_t offset,
                                                        std::size_t limit,
                                                        const core::CallContext& ctx) = 0;
    /** @brief 分批清理过期事件。
     * @param[in,out] tx 公共事务。 @param[in] before UTC 保留界限。
     * @param[in] ctx 期限。 @return 状态，每批最多 200 条。
     */
    virtual core::Status prune(ITransaction& tx, const std::string& before,
                               const core::CallContext& ctx) = 0;
};
}  // namespace ems::database
