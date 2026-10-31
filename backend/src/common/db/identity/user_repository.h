#pragma once
#include <common/database/identity/repositories.h>

#include "../configuration/orm_device_config_repository.h"
#include "identity_rows.h"

namespace ems::database::detail {
/** @brief 复用公共事务的身份及审计适配器；固定 ems_web 限定名。 */
template <class Driver>
class OrmIdentityRepository final : public IUserRepository, public IAuditRepository {
   public:
    using Acquire = typename OrmDeviceConfigRepository<Driver>::Acquire;
    using Invalidate = typename OrmDeviceConfigRepository<Driver>::Invalidate;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /** @brief 装配公共驱动访问。
     * @param[in] acquire 事务借用器。 @param[in] invalidate 失效处理。
     * @param[in] failure 错误转换。 @param[in] mysql 是否启用 MySQL 行锁。
     */
    OrmIdentityRepository(Acquire acquire, Invalidate invalidate, Failure failure,
                          bool mysql = true)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)),
          mysql_(mysql) {}

    /** @brief 读取策略。 @param[in,out] tx 事务。 @param[in] lock 写锁。
     * @param[in] ctx 期限。 @return 唯一策略或错误。 */
    core::Result<LoginPolicyRecord> policy(ITransaction& tx, bool lock,
                                           const core::CallContext& ctx) override {
        return run<LoginPolicyRecord>(tx, ctx, [&](auto& db) {
            auto rows = db.template query_s<LoginPolicyRecord>("id=1" + locking(lock));
            if (rows.size() != 1)
                throw core::make_error(core::ErrorCode::SchemaMismatch,
                                       core::WriteEffect::NotApplied, "identity policy missing");
            return rows.front();
        });
    }

    /** @brief 保存策略。 @param[in,out] tx 事务。 @param[in] row 策略。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status save_policy(ITransaction& tx, const LoginPolicyRecord& row,
                             const core::CallContext& ctx) override {
        return save(tx, row, ctx, false);
    }

    /** @brief 按用户名或 ID 读取用户。 @param[in,out] tx 事务。 @param[in] key 绑定值。
     * @param[in] by_name 使用用户名。 @param[in] lock 锁模式。 @param[in] ctx 期限。
     * @return 可空用户。 */
    core::Result<std::optional<UserRecord>> user(ITransaction& tx, const std::string& key,
                                                 bool by_name, IdentityLock lock,
                                                 const core::CallContext& ctx) override {
        return run<std::optional<UserRecord>>(tx, ctx, [&](auto& db) -> std::optional<UserRecord> {
            auto rows = db.template query_s<UserRecord>(
                std::string(by_name ? "username=?" : "user_id=?") +
                    (mysql_ ? (lock == IdentityLock::Update  ? " FOR UPDATE"
                               : lock == IdentityLock::Share ? " LOCK IN SHARE MODE"
                                                             : "")
                            : ""),
                key);
            if (rows.empty()) return std::nullopt;
            return rows.front();
        });
    }

    /** @brief 有界列表和统计。 @param[in,out] tx 事务。 @param[in] q 条件。
     * @param[in] now UTC。 @param[in] ctx 期限。 @return 列表及统计。 */
    core::Result<UserPage> users(ITransaction& tx, const UserQuery& q, const std::string& now,
                                 const core::CallContext& ctx) override {
        if (q.limit == 0 || q.limit > 200 || q.offset > 2000000 || q.keyword.size() > 256)
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplied);
        return run<UserPage>(tx, ctx, [&](auto& db) {
            UserPage page;
            auto counts = db.template query_s<
                std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t>>(
                "SELECT COUNT(*),COALESCE(SUM(CASE WHEN status='enabled' THEN 1 ELSE 0 END),0),"
                "COALESCE(SUM(CASE WHEN status='disabled' THEN 1 ELSE 0 END),0),"
                "COALESCE(SUM(CASE WHEN locked_until>? THEN 1 ELSE 0 END),0) "
                "FROM `ems_web`.`auth_user` WHERE status<>'deleted'",
                now);
            std::tie(page.total, page.enabled, page.disabled, page.locked) = counts.at(0);
            const std::string username = mysql_ ? "CONVERT(username USING utf8mb4)" : "username";
            const std::string where =
                "status<>'deleted' AND (?='' OR " + username +
                " LIKE ? ESCAPE '!' OR "
                "display_name LIKE ? ESCAPE '!') AND (?='' OR role_code=?) AND (?='' OR status=?)";
            std::string like = "%";
            for (char c : q.keyword) {
                if (c == '%' || c == '_' || c == '!') like += '!';
                like += c;
            }
            like += '%';
            auto matched = db.template query_s<std::tuple<std::int64_t>>(
                "SELECT COUNT(*) FROM `ems_web`.`auth_user` WHERE " + where, q.keyword, like, like,
                q.role_code, q.role_code, q.status, q.status);
            page.matched = std::get<0>(matched.at(0));
            page.items = db.template query_s<UserRecord>(
                where + " ORDER BY created_at,user_id LIMIT " + std::to_string(q.limit) +
                    " OFFSET " + std::to_string(q.offset),
                q.keyword, like, like, q.role_code, q.role_code, q.status, q.status);
            return page;
        });
    }

    /** @brief 计数。 @param[in,out] tx 事务。 @param[in] admins 仅启用管理员。
     * @param[in] ctx 期限。 @return 数量。 */
    core::Result<std::int64_t> count_users(ITransaction& tx, bool admins,
                                           const core::CallContext& ctx) override {
        return run<std::int64_t>(tx, ctx, [&](auto& db) {
            auto r = db.template query_s<std::tuple<std::int64_t>>(
                std::string("SELECT COUNT(*) FROM `ems_web`.`auth_user`") +
                (admins ? " WHERE role_code='admin' AND status='enabled'" : ""));
            return std::get<0>(r.at(0));
        });
    }

    /** @brief 保存内部用户。 @param[in,out] tx 事务。 @param[in] row 用户。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status save_user(ITransaction& tx, const UserRecord& row,
                           const core::CallContext& ctx) override {
        return save(tx, row, ctx, row.id == 0);
    }

    /** @brief 读取范围。 @param[in,out] tx 事务。 @param[in] user_id 用户。
     * @param[in] ctx 期限。 @return 范围。 */
    core::Result<std::vector<ScopeRecord>> scopes(ITransaction& tx, const std::string& user_id,
                                                  const core::CallContext& ctx) override {
        return run<std::vector<ScopeRecord>>(tx, ctx, [&](auto& db) {
            auto rows = db.template query_s<ScopeRecord>(
                "user_id=? ORDER BY station_id,device_root_id LIMIT 201", user_id);
            bounded(rows.size(), 200);
            return rows;
        });
    }

    /** @brief 替换范围。 @param[in,out] tx 事务。 @param[in] user_id 用户。
     * @param[in] rows 范围。 @param[in] ctx 期限。 @return 状态。 */
    core::Status replace_scopes(ITransaction& tx, const std::string& user_id,
                                const std::vector<ScopeRecord>& rows,
                                const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            bounded(rows.size(), 200);
            db.template delete_records_s<ScopeRecord>("user_id=?", user_id);
            for (auto row : rows) {
                row.id = 0;
                row.user_id = user_id;
                db.write(row, RecordWriteMode::Insert);
            }
            return core::Unit{};
        });
    }

    /** @brief 按摘要读取。 @param[in,out] tx 事务。 @param[in] hash 原始 32 字节。
     * @param[in] ctx 期限。 @return 可空会话。 */
    core::Result<std::optional<SessionRecord>> session(ITransaction& tx,
                                                       const std::vector<char>& hash,
                                                       const core::CallContext& ctx) override {
        return run<std::optional<SessionRecord>>(
            tx, ctx, [&](auto& db) -> std::optional<SessionRecord> {
                if (hash.size() != 32) return std::nullopt;
                auto rows = db.template query_s<SessionRecord>("token_hash=?", hash);
                if (rows.empty()) return std::nullopt;
                return rows.front();
            });
    }

    /** @brief 有界未撤销会话。 @param[in,out] tx 事务。 @param[in] user_id 用户。
     * @param[in] lock 写锁。 @param[in] ctx 期限。 @return 会话。 */
    core::Result<std::vector<SessionRecord>> sessions(ITransaction& tx, const std::string& user_id,
                                                      bool lock,
                                                      const core::CallContext& ctx) override {
        return run<std::vector<SessionRecord>>(tx, ctx, [&](auto& db) {
            auto rows = db.template query_s<SessionRecord>(
                "user_id=? AND revoked_at IS NULL ORDER BY last_interactive_at,session_id LIMIT "
                "1001" +
                    locking(lock),
                user_id);
            bounded(rows.size(), 1000);
            return rows;
        });
    }

    /** @brief 检查稳定会话归属。 @param[in,out] tx 事务。 @param[in] user_id 用户。
     * @param[in] session_id 会话。 @param[in] ctx 期限。 @return 可空会话。 */
    core::Result<std::optional<SessionRecord>> find_session(ITransaction& tx,
                                                            const std::string& user_id,
                                                            const std::string& session_id,
                                                            const core::CallContext& ctx) override {
        return run<std::optional<SessionRecord>>(
            tx, ctx, [&](auto& db) -> std::optional<SessionRecord> {
                auto rows = db.template query_s<SessionRecord>("user_id=? AND session_id=?",
                                                               user_id, session_id);
                if (rows.empty()) return std::nullopt;
                return rows.front();
            });
    }

    /** @brief 保存会话。 @param[in,out] tx 事务。 @param[in] row 会话。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status save_session(ITransaction& tx, const SessionRecord& row,
                              const core::CallContext& ctx) override {
        return save(tx, row, ctx, row.id == 0);
    }

    /** @brief 有界清理会话。 @param[in,out] tx 事务。 @param[in] before UTC。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status prune_sessions(ITransaction& tx, const std::string& before,
                                const core::CallContext& ctx) override {
        return prune_rows<SessionRecord>(tx, "absolute_expires_at", before, ctx);
    }

    /** @brief 追加审计。 @param[in,out] tx 事务。 @param[in] row 事件。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status append(ITransaction& tx, const AuditRecord& row,
                        const core::CallContext& ctx) override {
        return save(tx, row, ctx, true);
    }

    /** @brief 查询审计。 @param[in,out] tx 事务。 @param[in] offset 偏移。
     * @param[in] limit 限额。 @param[in] ctx 期限。 @return 事件。 */
    core::Result<std::vector<AuditRecord>> list(ITransaction& tx, std::size_t offset,
                                                std::size_t limit,
                                                const core::CallContext& ctx) override {
        return run<std::vector<AuditRecord>>(tx, ctx, [&](auto& db) {
            bounded(limit, 200);
            bounded(offset, 2000000);
            return db.template query_s<AuditRecord>("1=1 ORDER BY occurred_at DESC,id DESC LIMIT " +
                                                    std::to_string(limit) + " OFFSET " +
                                                    std::to_string(offset));
        });
    }

    /** @brief 有界清理审计。 @param[in,out] tx 事务。 @param[in] before UTC。
     * @param[in] ctx 期限。 @return 状态。 */
    core::Status prune(ITransaction& tx, const std::string& before,
                       const core::CallContext& ctx) override {
        return prune_rows<AuditRecord>(tx, "occurred_at", before, ctx);
    }

   private:
    /** @brief 构造固定行锁。 @param[in] lock 是否锁定。 @return SQL 后缀。 */
    std::string locking(bool lock) const { return lock && mysql_ ? " FOR UPDATE" : ""; }

    /** @brief 拒绝损坏或无界集合。 @param[in] n 数量。 @param[in] max 上限。 */
    static void bounded(std::size_t n, std::size_t max) {
        if (n > max)
            throw core::make_error(core::ErrorCode::ResourceExhausted,
                                   core::WriteEffect::NotApplied, "identity row budget");
    }

    /** @brief 受检执行。 @param[in,out] tx 事务。 @param[in] ctx 期限。
     * @param[in] op 内部操作。 @return 结果或脱敏错误。 */
    template <class T, class Op>
    core::Result<T> run(ITransaction& tx, const core::CallContext& ctx, Op op) {
        auto a = acquire_(tx, ctx);
        if (!core::is_ok(a)) return core::error_of(a);
        CheckedConfigurationSession<Driver> db(*core::value_of(a), ctx, failure_);
        try {
            return op(db);
        } catch (const core::Error& e) {
            invalidate_(tx);
            return e;
        } catch (...) {
            invalidate_(tx);
            return core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied,
                                    "identity repository failed");
        }
    }

    /** @brief 受检保存。 @param[in,out] tx 事务。 @param[in] row 记录。
     * @param[in] ctx 期限。 @param[in] insert 是否新增。 @return 状态。 */
    template <class Row>
    core::Status save(ITransaction& tx, const Row& row, const core::CallContext& ctx, bool insert) {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            db.write(row, insert ? RecordWriteMode::Insert : RecordWriteMode::Update);
            return core::Unit{};
        });
    }

    /** @brief 受检分批删除。 @param[in,out] tx 事务。 @param[in] field 固定时间列。
     * @param[in] before 保留界限。 @param[in] ctx 期限。 @return 状态。 */
    template <class Row>
    core::Status prune_rows(ITransaction& tx, const std::string& field, const std::string& before,
                            const core::CallContext& ctx) {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            auto rows = db.template query_s<Row>(field + "<? ORDER BY id LIMIT 200" + locking(true),
                                                 before);
            for (const auto& row : rows) db.template delete_records_s<Row>("id=?", row.id);
            return core::Unit{};
        });
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
    bool mysql_;
};
}  // namespace ems::database::detail
