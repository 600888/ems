#include <common/database/configuration/repository.h>

namespace ems::database {
core::Result<PointTableImportResult> import_configuration_points(
    IDatabase& database, IConfigurationRepository& repository, const std::string& path,
    std::int64_t channel_id, std::int64_t device_id, PointTableImportMode mode,
    const core::CallContext& ctx) {
    if (mode != PointTableImportMode::Append && mode != PointTableImportMode::Replace)
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "invalid point table import mode");
    auto begun = database.begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    auto tx = std::move(std::get<0>(begun));
    auto channel = repository.find_channel(*tx, channel_id, ctx);
    if (!core::is_ok(channel)) return core::error_of(channel);
    auto device = repository.find_device(*tx, device_id, ctx);
    if (!core::is_ok(device)) return core::error_of(device);
    if (core::value_of(channel).device_id != device_id)
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "channel does not belong to selected device");
    auto closed = tx->rollback(ctx);
    if (!core::is_ok(closed)) return core::error_of(closed);
    tx.reset();
    auto parsed =
        parse_point_table_excel(path, core::value_of(channel).protocol_type, channel_id, device_id);
    if (!parsed.ok()) return parsed;
    begun = database.begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    tx = std::move(std::get<0>(begun));
    auto staged = repository.stage_point_table(*tx, parsed.data, channel_id, device_id, mode, ctx);
    if (!core::is_ok(staged)) return core::error_of(staged);
    auto committed = tx->commit(ctx);
    if (!core::is_ok(committed)) return core::error_of(committed);
    parsed.data = std::move(core::value_of(staged));
    parsed.inserted = parsed.data.yc.size() + parsed.data.yx.size() + parsed.data.yk.size() +
                      parsed.data.yt.size();
    parsed.committed = true;
    return parsed;
}
}  // namespace ems::database
