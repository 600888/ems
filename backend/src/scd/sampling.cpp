#include "sampling.h"

#include <common/codec/digest.h>
#include <common/core/time.h>
#include <common/model/point.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace ems::storage {
namespace {
using Json = nlohmann::json;

core::Error invalid(const char* message) {
    return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable,
                            message);
}

database::ValueType value_type(const std::string& type) {
    if (type == "double") return database::ValueType::Numeric;
    if (type == "int64") return database::ValueType::Integer;
    if (type == "bool") return database::ValueType::Boolean;
    if (type == "string" || type == "uint64") return database::ValueType::Text;
    throw std::invalid_argument("unsupported archive value type");
}

void bad(database::Sample& sample, model::Quality quality, const char* reason) {
    sample.value = std::monostate{};
    sample.quality = static_cast<int>(quality);
    sample.quality_reason = reason;
}

database::Sample capture(const ArchivePoint& point, const ArchiveDirectory& directory,
                         std::int64_t slot_us, const PeriodicArchiveSampler::ReadPoint& reader,
                         const core::CallContext& ctx) {
    database::Sample sample;
    sample.point_id = point.series.point_id;
    sample.series_key = point.series.series_key;
    sample.value_type = point.series.value_type;
    sample.scope_kind = point.scope_kind;
    sample.scope_id = point.scope_id;
    sample.config_revision = std::to_string(directory.config_revision);
    sample.source_time_us = slot_us;
    sample.ingest_time_us = core::now_utc_micros();
    sample.source_id = "ems-storage";
    sample.source_epoch = "0";
    sample.sequence = std::to_string(slot_us);
    sample.time_origin = 1;
    sample.time_reason = "periodic_snapshot";
    sample.event_id = codec::sha256(sample.point_id + "|" + std::to_string(slot_us));
    if (!point.enabled) {
        bad(sample, model::Quality::Disabled, "configuration_disabled");
        return sample;
    }
    auto result = reader(point.point_type, point.series.point_id, ctx);
    if (!core::is_ok(result)) {
        bad(sample, model::Quality::CommFailure,
            core::error_of(result).code == core::ErrorCode::NotFound ? "sample_missing"
                                                                     : "realtime_unavailable");
        return sample;
    }
    try {
        const auto json = Json::parse(core::value_of(result));
        if (json.at("code") != sample.point_id || !json.at("timestamp").is_number_integer() ||
            !json.at("quality").is_number_integer())
            throw std::invalid_argument("sample identity or type");
        const auto& timestamp = json.at("timestamp");
        if (timestamp <= 0 || timestamp > std::numeric_limits<std::int64_t>::max() / 1000)
            throw std::invalid_argument("sample time");
        sample.measurement_time_us = timestamp.get<std::int64_t>() * 1000;
        if (sample.measurement_time_us > sample.ingest_time_us + 300000000)
            throw std::invalid_argument("future sample time");
        if (json.at("quality") != 0) {
            bad(sample, model::Quality::Invalid, "source_quality_invalid");
            return sample;
        }
        if (point.stale_after_ms > 0 &&
            (sample.ingest_time_us - sample.measurement_time_us) / 1000 > point.stale_after_ms) {
            bad(sample, model::Quality::Stale, "sample_stale");
            return sample;
        }
        const auto& value = json.at("value");
        switch (sample.value_type) {
            case database::ValueType::Numeric:
                if (!value.is_number() || !std::isfinite(value.get<double>()))
                    throw std::invalid_argument("numeric sample");
                sample.value = value.get<double>();
                break;
            case database::ValueType::Integer: {
                // 实时采集兼容 double，但超出其精确整数范围时拒绝伪装成无损整数。
                if (value.is_number_float()) {
                    const auto number = value.get<double>();
                    if (!std::isfinite(number) || std::trunc(number) != number ||
                        std::abs(number) > 9007199254740991.0)
                        throw std::invalid_argument("inexact integer sample");
                    sample.value = static_cast<std::int64_t>(number);
                } else if (value.is_number_integer() &&
                           (!value.is_number_unsigned() ||
                            value <= std::numeric_limits<std::int64_t>::max())) {
                    sample.value = value.get<std::int64_t>();
                } else {
                    throw std::invalid_argument("integer sample");
                }
                break;
            }
            case database::ValueType::Boolean:
                if (value.is_boolean())
                    sample.value = value.get<bool>();
                else if (value.is_number() &&
                         (value.get<double>() == 0.0 || value.get<double>() == 1.0))
                    sample.value = value.get<double>() == 1.0;
                else
                    throw std::invalid_argument("boolean sample");
                break;
            case database::ValueType::Text: {
                std::string text;
                if (point.unsigned_integer && value.is_number_float()) {
                    const auto number = value.get<double>();
                    if (!std::isfinite(number) || number < 0 || std::trunc(number) != number ||
                        number > 9007199254740991.0)
                        throw std::invalid_argument("inexact unsigned sample");
                    text = std::to_string(static_cast<std::uint64_t>(number));
                } else if (value.is_string() || value.is_number_integer()) {
                    text = value.is_string() ? value.get<std::string>() : value.dump();
                } else {
                    throw std::invalid_argument("text sample");
                }
                if (point.unsigned_integer) {
                    if (text.empty() || text.size() > 20 ||
                        text.find_first_not_of("0123456789") != std::string::npos)
                        throw std::invalid_argument("unsigned sample");
                    std::size_t consumed = 0;
                    (void)std::stoull(text, &consumed);
                    if (consumed != text.size()) throw std::invalid_argument("unsigned sample");
                }
                if (text.size() > 1024) throw std::invalid_argument("text sample length");
                sample.value = text;
                break;
            }
        }
    } catch (const std::exception&) {
        bad(sample, model::Quality::Invalid, "invalid_realtime_sample");
    }
    return sample;
}
}  // namespace

core::Result<ArchiveDirectory> build_archive_directory(
    const database::DeviceConfigRecords& records) {
    std::map<std::string, const database::CfgDeviceRow*> devices;
    const database::CfgDeviceRow* station = nullptr;
    for (const auto& device : records.devices) {
        if (device.device_id.empty() || !devices.emplace(device.device_id, &device).second)
            return invalid("duplicate device identity");
        if (device.node_kind == "station") {
            if (station) return invalid("unique station required");
            station = &device;
        }
    }
    if (!station || station->config_revision < 0) return invalid("station configuration missing");
    ArchiveDirectory directory{station->device_id, station->config_revision, {}};
    std::set<std::string> identities;
    const auto append = [&](const auto& points, const char* kind) {
        for (const auto& point : points) {
            if (point.point_id.empty() || point.point_id.size() > 64 ||
                !identities.insert(point.point_id).second)
                throw std::invalid_argument("duplicate point identity");
            ArchivePoint item;
            item.series = {point.point_id, value_type(point.value_type),
                           "p_" + codec::sha256(point.point_id)};
            item.point_type = kind;
            item.unsigned_integer = point.value_type == "uint64";
            item.scope_kind = "station";
            item.scope_id = station->device_id;
            item.enabled = point.enabled != 0;
            item.stale_after_ms =
                point.stale_after_ms > 0 ? point.stale_after_ms : station->default_stale_after_ms;
            auto id = point.owner_device_id;
            std::set<std::string> seen;
            for (;;) {
                const auto found = devices.find(id);
                if (found == devices.end() || !seen.insert(id).second || seen.size() > 9)
                    throw std::invalid_argument("invalid device hierarchy");
                const auto& device = *found->second;
                item.enabled = item.enabled && device.enabled != 0;
                if (device.node_kind == "unit" && item.scope_kind == "station") {
                    item.scope_kind = "unit";
                    item.scope_id = device.device_id;
                }
                if (device.node_kind == "station") {
                    if (device.device_id != station->device_id)
                        throw std::invalid_argument("station mismatch");
                    break;
                }
                id = device.parent_device_id;
            }
            directory.points.push_back(std::move(item));
        }
    };
    try {
        append(records.points_yc, "yc");
        append(records.points_yx, "yx");
        append(records.points_yk, "yk");
        append(records.points_yt, "yt");
    } catch (const std::exception&) {
        return invalid("invalid archive directory");
    }
    return directory;
}

ArchiveDirectorySource::ArchiveDirectorySource(database::MySqlDatabaseContext database)
    : database_(std::move(database)) {}

core::Result<ArchiveDirectory> ArchiveDirectorySource::refresh(const core::CallContext& ctx) {
    if (!database_.database || !database_.device_management || !database_.device_config ||
        !database_.history_catalog)
        return invalid("archive dependencies missing");
    auto begun = database_.database->begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    auto transaction = std::move(std::get<0>(begun));
    database::DeviceQuery query;
    query.filters = {{"node_kind", "station"}};
    query.limit = 2;
    query.for_share = true;
    auto roots = database_.device_management->query(*transaction, query, ctx);
    if (!core::is_ok(roots)) return core::error_of(roots);
    const auto& rows = core::value_of(roots).rows;
    if (rows.size() != 1 || !std::holds_alternative<database::CfgDeviceRow>(rows[0]))
        return invalid("unique station required");
    const auto& station = std::get<database::CfgDeviceRow>(rows[0]);
    if (directory_ && directory_->station_id == station.device_id &&
        directory_->config_revision == station.config_revision) {
        auto committed = transaction->commit(ctx);
        if (!core::is_ok(committed)) return core::error_of(committed);
        return *directory_;
    }
    auto records = database_.device_config->load_configuration(*transaction, ctx);
    if (!core::is_ok(records)) return core::error_of(records);
    auto built = build_archive_directory(core::value_of(records));
    if (!core::is_ok(built)) return core::error_of(built);
    for (auto& point : std::get<0>(built).points) {
        auto existing =
            database_.history_catalog->find_series(*transaction, point.series.point_id, ctx);
        if (core::is_ok(existing)) {
            const auto& saved = core::value_of(existing);
            if (saved.point_id != point.series.point_id ||
                saved.value_type != point.series.value_type)
                return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                        "point series type or identity changed");
            point.series = saved;
            continue;
        }
        if (core::error_of(existing).code != core::ErrorCode::NotFound)
            return core::error_of(existing);
        auto registered = database_.history_catalog->ensure_series(*transaction, point.series, ctx);
        if (!core::is_ok(registered)) return core::error_of(registered);
    }
    auto committed = transaction->commit(ctx);
    if (!core::is_ok(committed)) return core::error_of(committed);
    directory_ = std::move(std::get<0>(built));
    return *directory_;
}

PeriodicArchiveSampler::PeriodicArchiveSampler(ReadPoint reader, database::ITimeSeriesStore& series)
    : reader_(std::move(reader)), series_(series) {}

ArchiveReport PeriodicArchiveSampler::sample(const ArchiveDirectory& directory,
                                             std::int64_t slot_us, const core::CallContext& ctx) {
    ArchiveReport report;
    if (slot_us <= last_slot_us_) {
        report.duplicate_slot = true;
        return report;
    }
    last_slot_us_ = slot_us;
    for (const auto& point : directory.points) {
        if (core::has_expired(ctx)) {
            report.failed = directory.points.size() - report.written;
            break;
        }
        auto sample = capture(point, directory, slot_us, reader_, ctx);
        Json value;
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (!std::is_same_v<T, std::monostate>) value = v;
            },
            sample.value);
        sample.payload_sha256 = codec::sha256(Json{
            {"point_id", sample.point_id},
            {"slot_us", slot_us},
            {"measurement_us", sample.measurement_time_us},
            {"value", value},
            {"quality", sample.quality},
            {"revision", sample.config_revision}}.dump());
        if (sample.quality != 0) ++report.invalid;
        auto written = series_.write(sample, ctx);
        if (core::is_ok(written))
            ++report.written;
        else
            ++report.failed;
    }
    return report;
}
}  // namespace ems::storage
