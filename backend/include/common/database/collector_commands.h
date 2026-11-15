#pragma once

#include <common/database/i_database.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ems::database {

/** @brief 采集命令的持久执行记录；配置只引用站根 config_revision。 */
struct CollectorCommandRow {
    std::int64_t id{0};
    std::string command_id;
    std::string scope_code;
    std::string point_id;
    double value{0};
    std::int64_t config_revision{0};
    std::int64_t expires_at_ms{0};
    std::string payload_json;
    std::string status;
    std::string reason;
    std::int64_t sent_at_ms{0};
    std::int64_t updated_at_ms{0};
};

/** @brief 命令 inbox 与查询使用同一记录，所有操作复用调用方事务。 */
class ICollectorCommandRepository {
   public:
    /** @brief 释放仓储。 */
    virtual ~ICollectorCommandRepository() = default;

    /**
     * @brief 按稳定 ID 查询，更新前可加排他锁。
     * @param[in,out] tx 调用方事务。
     * @param[in] scope 采集作用域。
     * @param[in] id 稳定命令 ID。
     * @param[in] lock 是否锁定记录。
     * @param[in] ctx 数据库调用期限。
     * @return 可空记录或数据库错误。
     */
    virtual core::Result<std::optional<CollectorCommandRow>> find(ITransaction& tx,
                                                                  const std::string& scope,
                                                                  const std::string& id, bool lock,
                                                                  const core::CallContext& ctx) = 0;

    /**
     * @brief 插入或更新已锁定的记录；唯一键冲突不能覆盖旧命令。
     * @param[in,out] tx 调用方事务。
     * @param[in] row 待保存记录，id=0 时仅插入。
     * @param[in] ctx 数据库调用期限。
     * @return 暂存结果；调用方须检查 commit，提交未知时不得发送设备动作。
     */
    virtual core::Status save(ITransaction& tx, const CollectorCommandRow& row,
                              const core::CallContext& ctx) = 0;

    /**
     * @brief 按数据库 ID 分页读取未决命令，固定每页最多 128 条。
     * @param[in,out] tx 调用方事务。
     * @param[in] scope 采集作用域。
     * @param[in] after 上一页最后一个数据库 ID，首屏为 0。
     * @param[in] ctx 数据库调用期限。
     * @return Queued/Executing/Acked/Unknown 的有界记录集或错误。
     */
    virtual core::Result<std::vector<CollectorCommandRow>> pending(
        ITransaction& tx, const std::string& scope, std::int64_t after,
        const core::CallContext& ctx) = 0;
};
}  // namespace ems::database
