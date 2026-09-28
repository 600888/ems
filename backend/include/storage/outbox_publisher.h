#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/database/i_database.h>
#include <common/database/repositories.h>

#include <cstdint>
#include <string>

namespace ems::storage {

class IOutboxSink {
   public:
    /** @brief 销毁 outbox 发布目标接口。 */
    virtual ~IOutboxSink() = default;

    /**
     * @brief 按稳定 event_id 发布已提交业务事件；重复调用必须安全。
     * @param[in] item 带租约的 outbox 记录。
     * @param[in] ctx 调用截止时间。
     * @return 仅在目标确认受理时返回成功；结果不明时返回 Unknown。
     */
    virtual core::Status publish(const database::ClaimedOutboxItem& item,
                                 const core::CallContext& ctx) = 0;
};

struct OutboxReport {
    std::uint32_t claimed{0};
    std::uint32_t published{0};
    std::uint32_t deferred{0};
};

class OutboxPublisher {
   public:
    /**
     * @brief 创建有界 outbox 发布器，依赖由调用方持有。
     * @param[in,out] database MySQL 事务入口。
     * @param[in,out] repository outbox 仓储。
     * @param[in,out] sink 按 topic 路由的发布目标。
     * @param[in] claimer_id 本实例领取身份。
     * @param[in] claim_timeout_ms 租约超时毫秒数。
     * @param[in] max_items 单次最多领取条数。
     */
    OutboxPublisher(database::IDatabase& database, database::IOutboxRepository& repository,
                    IOutboxSink& sink, std::string claimer_id, std::int64_t claim_timeout_ms,
                    std::uint32_t max_items);

    /**
     * @brief 短事务领取、提交、发布，再逐条事务确认发布状态。
     * @param[in] ctx 调用截止时间。
     * @return 领取提交失败返回错误；单条失败留待租约到期重试并计入 deferred。
     */
    core::Result<OutboxReport> publish_batch(const core::CallContext& ctx);

   private:
    database::IDatabase& database_;
    database::IOutboxRepository& repository_;
    IOutboxSink& sink_;
    std::string claimer_id_;
    std::int64_t claim_timeout_ms_;
    std::uint32_t max_items_;
};

}  // namespace ems::storage
