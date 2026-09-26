/**
 * @file repositories.h
 * @brief 共享 Repository 接口（design.md 第 8.1/8.2 节）
 *
 * 所有变更方法显式接收调用方的 ITransaction&，禁止在内部偷偷新建
 * 另一个连接或提交事务。业务与 outbox 同事务原子提交：
 *   begin → repository.save(tx, stable_business_id, expected_version, data)
 *         → outbox.append(tx, same_event_id, envelope) → commit。
 *
 * 唯一键冲突映射 Conflict；对象不存在为 NotFound；查询必须分页并
 * 限制返回数量；SQL 参数必须绑定，动态排序/列名只能来自白名单。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/identity.h>
#include <common/core/result.h>
#include <common/database/i_transaction.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ems::database {

struct ConfigVersionRow {
    std::string scope;
    std::string config_version;
    std::string base_version;  ///< 编辑基版本（乐观并发检查）
    std::string payload_json;  ///< 版本化确定性编码的发布载荷
    std::string checksum;      ///< SHA-256 hex
    std::string created_by;
    std::int64_t created_at_us{0};
};

struct ActiveConfigRow {
    std::string scope;
    std::string config_version;
    std::string activation_revision;
    std::int64_t activated_at_us{0};
};

/** @brief 按稳定 ID 与版本保存和读取不可变配置。 */
class IConfigRepository {
   public:
    /** @brief 销毁配置仓储接口。 */
    virtual ~IConfigRepository() = default;

    /// 保存不可变配置版本；同版本重复返回幂等成功，同版本不同内容返回 Conflict
    /// @param[in,out] tx 数据库事务。
    /// @param[in] row 数据行。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status save_config_version(ITransaction& tx, const ConfigVersionRow& row,
                                             const core::CallContext& ctx) = 0;

    /// 读取指定版本配置载荷
    /// @param[out] tx 数据库事务。
    /// @param[in] scope 记录作用域。
    /// @param[in] config_version 配置版本。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<ConfigVersionRow> get_config_version(ITransaction& tx,
                                                              const std::string& scope,
                                                              const std::string& config_version,
                                                              const core::CallContext& ctx) = 0;

    /// 条件切换 active；expected_activation_revision 不符返回 Conflict
    /// @param[in,out] tx 数据库事务。
    /// @param[in] scope 记录作用域。
    /// @param[in] config_version 配置版本。
    /// @param[in] expected_activation_revision 期望的激活修订号。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<ActiveConfigRow> activate(ITransaction& tx, const std::string& scope,
                                                   const std::string& config_version,
                                                   const std::string& expected_activation_revision,
                                                   const core::CallContext& ctx) = 0;

    /// 读取 active 清单
    /// @param[out] tx 数据库事务。
    /// @param[in] scope 记录作用域。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<ActiveConfigRow> get_active(ITransaction& tx, const std::string& scope,
                                                     const core::CallContext& ctx) = 0;
};

/// 意图/命令/回执的持久化载荷（业务状态机检查由所属业务服务执行；
/// Repository 只提供状态/版本条件更新，不暴露"任意状态可覆盖"的 CRUD）
struct IntentRow {
    std::string intent_id;
    std::string capability_code;
    std::string target_asset_id;
    std::string actor;
    std::string reason;
    std::string payload_json;
    std::int64_t created_at_us{0};
};

struct CommandRow {
    std::string command_id;
    std::string intent_id;
    std::string target_asset_id;
    std::string expected_config_version;
    std::uint64_t owner_epoch{0};
    std::string status;  ///< model::to_string(CommandStatus)
    std::string payload_json;
    std::int64_t created_at_us{0};
};

struct CommandResultRow {
    std::string command_id;
    std::string status;
    std::string reason;
    std::string protocol_receipt;
    std::int64_t result_time_us{0};
};

/** @brief 追加控制意图、命令和回执并按版本推进状态。 */
class IControlRepository {
   public:
    /** @brief 销毁控制仓储接口。 */
    virtual ~IControlRepository() = default;

    /// 追加意图；event/intent ID 唯一约束兜底，重复同载荷幂等，异载荷 Conflict
    /// @param[in,out] tx 数据库事务。
    /// @param[in] intent 写入意图。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status append_intent(ITransaction& tx, const IntentRow& intent,
                                       const core::CallContext& ctx) = 0;

    /// 追加命令；同 ID 不同载荷属于冲突，不能悄悄覆盖
    /// @param[in,out] tx 数据库事务。
    /// @param[in] command 待执行的设备命令。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status append_command(ITransaction& tx, const CommandRow& command,
                                        const core::CallContext& ctx) = 0;

    /// 条件推进命令状态（expected_status 不符返回 Conflict）
    /// @param[in,out] tx 数据库事务。
    /// @param[in] command_id command_id 的标识。
    /// @param[in] expected_status 期望的当前状态。
    /// @param[in] new_status 目标状态。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status advance_command(ITransaction& tx, const std::string& command_id,
                                         const std::string& expected_status,
                                         const std::string& new_status,
                                         const core::CallContext& ctx) = 0;

    /// 追加回执（按状态机追加事件；审计原子保存）
    /// @param[in,out] tx 数据库事务。
    /// @param[out] result 计算或执行结果。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status append_command_result(ITransaction& tx, const CommandResultRow& result,
                                               const core::CallContext& ctx) = 0;

    /// 查询稳定 ID 已有结果（提交响应丢失后按原 ID 对账）
    /// @param[out] tx 数据库事务。
    /// @param[in] command_id command_id 的标识。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<CommandResultRow> find_command_result(ITransaction& tx,
                                                               const std::string& command_id,
                                                               const core::CallContext& ctx) = 0;
};

struct AlarmEventRow {
    std::string event_id;
    std::string alarm_code;
    std::string asset_id;
    std::string lifecycle;
    std::string acknowledged_by;
    std::string detail;
    std::int64_t event_time_us{0};
    std::int64_t ingest_time_us{0};
};

/** @brief 保存告警生命周期事件并查询告警投影。 */
class IAlarmRepository {
   public:
    /** @brief 销毁告警仓储接口。 */
    virtual ~IAlarmRepository() = default;

    /// 追加告警生命周期事件；event_id 唯一，重复幂等
    /// @param[in,out] tx 数据库事务。
    /// @param[in] event 事件数据。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status append_alarm_event(ITransaction& tx, const AlarmEventRow& event,
                                            const core::CallContext& ctx) = 0;

    /// 查询未恢复告警投影（分页，limit 必须 > 0）
    /// @param[in,out] tx 数据库事务。
    /// @param[in] scope 记录作用域。
    /// @param[in] before_time_us 清理截止时间，单位为微秒。
    /// @param[in] limit 结果数量上限。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<std::vector<AlarmEventRow>> list_active_alarms(
        ITransaction& tx, const std::string& scope, std::int64_t before_time_us,
        std::uint32_t limit, const core::CallContext& ctx) = 0;
};

struct OutboxRecord {
    std::string event_id;         ///< 唯一；同 ID 不同载荷属于冲突
    std::string business_ref_id;  ///< 业务关联稳定 ID
    std::string scope;
    std::string topic;
    std::string schema_version;
    std::string payload;  ///< 不可变载荷
    std::int64_t created_at_us{0};
};

struct ClaimedOutboxItem {
    std::int64_t outbox_id{0};
    OutboxRecord record;
    std::string claim_token;  ///< 独立领取令牌；确认发布时校验
    std::uint32_t attempts{0};
};

/** @brief 在业务事务中追加 outbox，并支持发布端领取与确认。 */
class IOutboxRepository {
   public:
    /** @brief 销毁 outbox 仓储接口。 */
    virtual ~IOutboxRepository() = default;

    /// 与业务写同一事务 append；event_id 唯一约束兜底
    /// @param[in,out] tx 数据库事务。
    /// @param[in] record 记录数据。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status append(ITransaction& tx, const OutboxRecord& record,
                                const core::CallContext& ctx) = 0;

    /// 短事务有界领取（claim_timeout 内到期未确认可被接管）
    /// @param[in,out] tx 数据库事务。
    /// @param[in] claimer_id claimer_id 的标识。
    /// @param[in] claim_timeout_ms 领取超时时间，单位为毫秒。
    /// @param[in] max_items 允许的最大数据项数。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<std::vector<ClaimedOutboxItem>> claim(ITransaction& tx,
                                                               const std::string& claimer_id,
                                                               std::int64_t claim_timeout_ms,
                                                               std::uint32_t max_items,
                                                               const core::CallContext& ctx) = 0;

    /// 确认发布；校验领取令牌，防止旧领取者覆盖新状态
    /// @param[in,out] tx 数据库事务。
    /// @param[in] outbox_id outbox_id 的标识。
    /// @param[in] claim_token 领取凭据。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 得到的操作状态。
    virtual core::Status mark_published(ITransaction& tx, std::int64_t outbox_id,
                                        const std::string& claim_token,
                                        const core::CallContext& ctx) = 0;

    /// 最旧未完成记录时长（指标：outbox 最旧未完成时长）
    /// @param[in,out] tx 数据库事务。
    /// @param[in] ctx 调用上下文，包含截止时间等信息。
    /// @return 成功时包含结果；失败时包含错误信息。
    virtual core::Result<std::int64_t> oldest_pending_age_ms(ITransaction& tx,
                                                             const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
