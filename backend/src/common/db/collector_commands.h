#pragma once

#include <common/database/collector_commands.h>

#include "configuration/orm_device_config_repository.h"

namespace ems::database {
/** @brief 返回命令运行表名。 @return 固定表名。 */
constexpr std::string_view get_alias_struct_name(CollectorCommandRow*) { return "ops_command"; }
YLT_REFL(CollectorCommandRow, id, command_id, scope_code, point_id, value, config_revision,
         expires_at_ms, payload_json, status, reason, sent_at_ms, updated_at_ms)
inline const auto collector_command_auto =
    ormpp::add_auto_key_field(ylt::reflection::type_string<CollectorCommandRow>(), "id");
inline const auto collector_command_key =
    ormpp::add_conflict_key_field(ylt::reflection::type_string<CollectorCommandRow>(), "id");

namespace detail {
/** @brief SQL 行锁能力；不以数据库品牌区分，SQLite 由事务锁保证写入互斥。 */
enum class CommandRowLock { ForUpdate, Transaction };

/** @brief 使用公共事务池的命令记录实现，SQLite 测试共用相同算法。 */
template <class Driver>
class OrmCollectorCommandRepository final : public ICollectorCommandRepository {
   public:
    using Acquire = typename OrmDeviceConfigRepository<Driver>::Acquire;
    using Invalidate = typename OrmDeviceConfigRepository<Driver>::Invalidate;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /**
     * @brief 注入公共连接及错误转换。
     * @param[in] acquire 事务连接借用器。
     * @param[in] invalidate 失败事务失效回调。
     * @param[in] failure 驱动错误转换器。
     * @param[in] row_lock 行锁能力；MySQL/PostgreSQL 使用 ForUpdate，SQLite 使用 Transaction。
     */
    OrmCollectorCommandRepository(Acquire acquire, Invalidate invalidate, Failure failure,
                                  CommandRowLock row_lock = CommandRowLock::ForUpdate)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)),
          row_lock_(row_lock) {}

    /**
     * @brief 读取命令记录。
     * @param[in,out] tx 事务。
     * @param[in] scope 作用域。
     * @param[in] id 命令 ID。
     * @param[in] lock 是否加写锁。
     * @param[in] ctx 期限。
     * @return 可空记录或错误。
     */
    core::Result<std::optional<CollectorCommandRow>> find(ITransaction& tx,
                                                          const std::string& scope,
                                                          const std::string& id, bool lock,
                                                          const core::CallContext& ctx) override {
        return run<std::optional<CollectorCommandRow>>(
            tx, ctx, [&](auto& db) -> std::optional<CollectorCommandRow> {
                auto rows = db.template query_s<CollectorCommandRow>(
                    std::string("scope_code=? AND command_id=?") +
                        (row_lock_ == CommandRowLock::ForUpdate && lock ? " FOR UPDATE" : ""),
                    scope, id);
                if (rows.empty()) return std::nullopt;
                return rows.front();
            });
    }

    /**
     * @brief 保存命令记录。
     * @param[in,out] tx 事务。
     * @param[in] row 已锁定记录或新记录。
     * @param[in] ctx 期限。
     * @return 暂存结果。
     */
    core::Status save(ITransaction& tx, const CollectorCommandRow& row,
                      const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            db.write(row, row.id ? RecordWriteMode::Update : RecordWriteMode::Insert);
            return core::Unit{};
        });
    }

    /**
     * @brief 分页扫描未决命令。
     * @param[in,out] tx 事务。
     * @param[in] scope 作用域。
     * @param[in] after 上页 ID。
     * @param[in] ctx 期限。
     * @return 有界记录集或错误。
     */
    core::Result<std::vector<CollectorCommandRow>> pending(ITransaction& tx,
                                                           const std::string& scope,
                                                           std::int64_t after,
                                                           const core::CallContext& ctx) override {
        return run<std::vector<CollectorCommandRow>>(tx, ctx, [&](auto& db) {
            return db.template query_s<CollectorCommandRow>(
                "scope_code=? AND id>? AND (status IN ('Queued','Executing','Unknown') OR "
                "(status='Acked' AND reason='feedback_pending')) "
                "ORDER BY id LIMIT 128",
                scope, after);
        });
    }

   private:
    /**
     * @brief 受检执行内部 ORM 操作。
     * @param[in,out] tx 事务。
     * @param[in] ctx 期限。
     * @param[in] op 内部操作。
     * @return 结果或脱敏错误。
     */
    template <class T, class Op>
    core::Result<T> run(ITransaction& tx, const core::CallContext& ctx, Op op) {
        auto acquired = acquire_(tx, ctx);
        if (!core::is_ok(acquired)) return core::error_of(acquired);
        CheckedConfigurationSession<Driver> db(*core::value_of(acquired), ctx, failure_);
        try {
            return op(db);
        } catch (const core::Error& error) {
            invalidate_(tx);
            return error;
        } catch (...) {
            invalidate_(tx);
            return core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied,
                                    "command repository failed");
        }
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
    CommandRowLock row_lock_;
};
}  // namespace detail
}  // namespace ems::database
