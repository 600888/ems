#include "outbox_publisher.h"

#include <memory>
#include <utility>

namespace ems::storage {

OutboxPublisher::OutboxPublisher(database::IDatabase& database,
                                 database::IOutboxRepository& repository, IOutboxSink& sink,
                                 std::string claimer_id, std::int64_t claim_timeout_ms,
                                 std::uint32_t max_items)
    : database_(database),
      repository_(repository),
      sink_(sink),
      claimer_id_(std::move(claimer_id)),
      claim_timeout_ms_(claim_timeout_ms),
      max_items_(max_items) {}

core::Result<OutboxReport> OutboxPublisher::publish_batch(const core::CallContext& ctx) {
    if (claimer_id_.empty() || claim_timeout_ms_ <= 0 || max_items_ == 0 || max_items_ > 128) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "outbox publisher bounds");
    }
    auto opened = database_.begin(ctx);
    if (!core::is_ok(opened)) return core::error_of(opened);
    auto transaction = std::move(std::get<std::unique_ptr<database::ITransaction>>(opened));
    auto claimed = repository_.claim(*transaction, claimer_id_, claim_timeout_ms_, max_items_, ctx);
    if (!core::is_ok(claimed)) return core::error_of(claimed);
    auto committed = transaction->commit(ctx);
    if (!core::is_ok(committed)) return core::error_of(committed);
    OutboxReport report;
    report.claimed = static_cast<std::uint32_t>(core::value_of(claimed).size());
    for (const auto& item : core::value_of(claimed)) {
        auto sent = sink_.publish(item, ctx);
        if (!core::is_ok(sent)) {
            ++report.deferred;
            continue;
        }
        auto mark_tx = database_.begin(ctx);
        if (!core::is_ok(mark_tx)) {
            ++report.deferred;
            continue;
        }
        auto confirming = std::move(std::get<std::unique_ptr<database::ITransaction>>(mark_tx));
        auto marked =
            repository_.mark_published(*confirming, item.outbox_id, item.claim_token, ctx);
        if (!core::is_ok(marked)) {
            ++report.deferred;
            continue;
        }
        auto confirmed = confirming->commit(ctx);
        if (!core::is_ok(confirmed)) {
            ++report.deferred;
            continue;
        }
        ++report.published;
    }
    return report;
}

}  // namespace ems::storage
