#include "alarm_archiver.h"

namespace ems::storage {

core::Result<std::size_t> archive_alarm_batch(database::IDatabase& db,
                                              database::IAlarmRepository& alarms,
                                              database::IAlarmTimeSeriesStore& series,
                                              const core::StationId& station_id,
                                              const core::CallContext& ctx) {
    constexpr std::size_t batch_size = 32;
    if (station_id.empty())
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "alarm archive station required");

    for (std::size_t i = 0; i < batch_size; ++i) {
        if (core::has_expired(ctx))
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied);
        auto source = db.begin(ctx);
        if (!core::is_ok(source)) return core::error_of(source);
        auto read_tx = std::get<0>(std::move(source));
        const auto result = alarms.next_for_archive(*read_tx, station_id, ctx);
        if (!core::is_ok(result)) return core::error_of(result);
        auto released = read_tx->rollback(ctx);
        if (!core::is_ok(released)) return core::error_of(released);
        read_tx.reset();
        const auto& item = core::value_of(result);
        if (!item) return i;
        if (item->station_id != station_id)
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplied,
                                    "alarm archive station mismatch");

        const auto written = series.write_alarm(*item, ctx);
        if (!core::is_ok(written)) return core::error_of(written);

        auto target = db.begin(ctx);
        if (!core::is_ok(target)) return core::error_of(target);
        auto mark_tx = std::get<0>(std::move(target));
        const auto marked = alarms.mark_archived(*mark_tx, *item, ctx);
        if (!core::is_ok(marked)) return core::error_of(marked);
        const auto committed = mark_tx->commit(ctx);
        if (!core::is_ok(committed)) return core::error_of(committed);
    }
    return batch_size;
}

}  // namespace ems::storage
