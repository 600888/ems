#include <common/core/time.h>
#include <rtdb/client/point_client.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace ems::rtdb {
namespace {
using Json = nlohmann::json;

Result<model::PointValue> typed_value(double value, model::ValueType type) {
    if (!std::isfinite(value))
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied, "non-finite sample"};
    switch (type) {
        case model::ValueType::Bool:
            if (value == 0 || value == 1) return model::PointValue{value != 0};
            break;
        case model::ValueType::Double:
            return model::PointValue{value};
        case model::ValueType::Int64:
            if (std::trunc(value) == value && std::abs(value) <= 9007199254740991.0)
                return model::PointValue{static_cast<std::int64_t>(value)};
            break;
        case model::ValueType::UInt64:
            if (std::trunc(value) == value && value >= 0 && value <= 9007199254740991.0)
                return model::PointValue{static_cast<std::uint64_t>(value)};
            break;
        case model::ValueType::String:
            break;
    }
    return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                 "sample cannot be represented by configured type"};
}

const char* type_name(model::PointType type) {
    switch (type) {
        case model::PointType::YC:
            return "YC";
        case model::PointType::YX:
            return "YX";
        case model::PointType::YK:
            return "YK";
        case model::PointType::YT:
            return "YT";
    }
    return "";
}
}  // namespace

struct PointClient::Impl {
    std::shared_ptr<RealtimeClient> client;
    std::shared_ptr<const RuntimeCatalog> catalog;
    std::unique_ptr<RealtimeService> service;
    std::shared_mutex mutex;
    core::SourceEpoch epoch{static_cast<std::uint64_t>(core::now_utc_micros())};
    std::atomic<core::Sequence> sequence{0};
    bool writer{false};

    model::PointSample sample(model::PointValue value, model::Quality quality, std::int64_t time) {
        model::PointSample result;
        result.value = std::move(value);
        result.quality.code = quality;
        result.sequence = ++sequence;
        result.source_epoch = epoch;
        result.source_id = "ems-collector";
        result.config_version = catalog->configVersion();
        result.source_time = time > 0 && time <= std::numeric_limits<std::int64_t>::max() / 1000
                                 ? time * 1000
                                 : core::kInvalidUtcMicros;
        result.ingest_time = core::now_utc_micros();
        return result;
    }
};

PointClient::PointClient(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

PointClient::~PointClient() = default;

Result<std::shared_ptr<PointClient>> PointClient::open(
    const std::string& path, std::shared_ptr<const RuntimeCatalog> catalog,
    const CallContext& ctx) {
    if (!catalog)
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                     "runtime catalog required"};
    auto opened = RealtimeClient::open(path, ctx);
    if (!core::is_ok(opened)) return core::error_of(opened);
    auto impl = std::make_unique<Impl>();
    impl->client = std::move(std::get<0>(opened));
    impl->catalog = std::move(catalog);
    impl->service = std::make_unique<RealtimeService>(impl->client, impl->catalog);
    return std::shared_ptr<PointClient>(new PointClient(std::move(impl)));
}

core::Status PointClient::installWriter(const CallContext& ctx) {
    std::unique_lock lock(impl_->mutex);
    impl_->writer = false;
    for (const auto& point : impl_->catalog->points()) {
        SourcePolicy source{"ems-collector", impl_->epoch, impl_->catalog->configVersion(),
                            point.enabled};
        auto authorized = impl_->service->authorizePoint(point.address, source, ctx);
        if (!core::is_ok(authorized)) return core::error_of(authorized);
        if (model::is_command(point.point_type)) {
            const RecordKey key{
                point.address.scope, RecordKind::CommandRoute, {}, point.point_id.value()};
            Fields fields{
                {"point_id", point.point_id.value()},
                {"device_id", point.control_device_id},
                {"config_revision", impl_->catalog->configVersion()},
                {"enabled", point.enabled && !point.control_device_id.empty() ? "1" : "0"}};
            bool installed = false;
            for (unsigned attempt = 0; attempt < 8; ++attempt) {
                auto old = impl_->client->read(key, ctx);
                if (!core::is_ok(old) && core::error_of(old).code != ErrorCode::NotFound)
                    return core::error_of(old);
                auto written = impl_->client->compareExchange(
                    {key, fields,
                     core::is_ok(old) ? std::optional<Revision>{core::value_of(old).revision}
                                      : std::nullopt},
                    ctx);
                if (core::is_ok(written)) {
                    installed = true;
                    break;
                }
                if (core::error_of(written).code != ErrorCode::Conflict)
                    return core::error_of(written);
            }
            if (!installed)
                return core::make_error(ErrorCode::Conflict, WriteEffect::NotApplied,
                                        "command route installation conflicted");
        }
    }
    impl_->writer = true;
    return core::ok_status();
}

Result<SampleResult> PointClient::update(const std::string& id, double value,
                                         model::Quality quality, std::int64_t time,
                                         const CallContext& ctx) {
    std::shared_lock lock(impl_->mutex);
    const auto* point = impl_->catalog->findPoint(core::PointId{id});
    if (!point) return Error{ErrorCode::NotFound, WriteEffect::NotApplied, "point not configured"};
    if (!impl_->writer)
        return Error{ErrorCode::PermissionDenied, WriteEffect::NotApplied, "writer not installed"};
    if (!model::is_measured(point->point_type))
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplied,
                     "command points require command projection"};
    auto typed = typed_value(value, point->value_type);
    if (!core::is_ok(typed)) return core::error_of(typed);
    return impl_->service->updatePoint(point->address,
                                       impl_->sample(core::value_of(typed), quality, time), ctx);
}

Result<std::string> PointClient::readPointJson(const std::string& type, const std::string& id,
                                               const CallContext& ctx) {
    const auto* point = impl_->catalog->findPoint(core::PointId{id});
    if (!point)
        return Error{ErrorCode::NotFound, WriteEffect::NotApplicable, "point not configured"};
    std::string expected = type_name(point->point_type);
    for (auto& c : expected) c = static_cast<char>(c + ('a' - 'A'));
    if (type != expected)
        return Error{ErrorCode::InvalidArgument, WriteEffect::NotApplicable, "point type mismatch"};
    auto read = impl_->service->getPoint(point->address, ctx);
    if (!core::is_ok(read)) return core::error_of(read);
    const auto& snapshot = core::value_of(read);
    if (!snapshot.value)
        return Error{ErrorCode::NotFound, WriteEffect::NotApplicable, "point has no sample"};
    Json value;
    std::visit([&](const auto& input) { value = input; }, *snapshot.value);
    auto quality = snapshot.quality.code;
    if (!snapshot.config_current || !snapshot.source_authorized) quality = model::Quality::Invalid;
    Json json{{"code", id},
              {"point_type", type_name(point->point_type)},
              {"value", value},
              {"quality", static_cast<int>(quality)},
              {"timestamp", snapshot.source_time > 0 ? snapshot.source_time / 1000
                                                     : snapshot.ingest_time / 1000}};
    if (!snapshot.command_id.empty()) {
        json["command_id"] = snapshot.command_id;
        json["command_status"] = snapshot.command_status;
        json["command_reason"] = snapshot.command_reason;
        json["value_role"] = "command_target";
    }
    return json.dump();
}

Result<SampleResult> PointClient::projectCommand(const CommandRecord& command, std::int64_t order,
                                                 const CallContext& ctx) {
    std::shared_lock lock(impl_->mutex);
    const auto* point = impl_->catalog->findPoint(core::PointId{command.request.point_id});
    if (!point || !point->enabled) return SampleResult::IgnoredOutOfOrder;
    if (!impl_->writer)
        return Error{ErrorCode::PermissionDenied, WriteEffect::NotApplied, "writer not installed"};
    auto value = typed_value(command.request.value, point->value_type);
    const auto quality = command.status == "Acked" || command.status == "Succeeded"
                             ? model::Quality::Good
                             : model::Quality::Uncertain;
    auto sample =
        impl_->sample(core::is_ok(value) ? core::value_of(value) : model::PointValue{false},
                      quality, command.updated_at_ms);
    if (!core::is_ok(value)) {
        sample.value.reset();
        sample.quality.code = model::Quality::Invalid;
    }
    sample.quality.reason = "command_" + command.status;
    return impl_->service->projectCommand(point->address, sample, command.request.command_id,
                                          command.status, command.reason, order,
                                          command.updated_at_ms, ctx);
}

core::Status PointClient::reportChannel(const std::string& id, ChannelRunState state,
                                        const std::string& reason, const CallContext& ctx) {
    std::shared_lock lock(impl_->mutex);
    if (!impl_->writer)
        return core::make_error(ErrorCode::PermissionDenied, WriteEffect::NotApplied,
                                "writer not installed");
    ChannelStateSample sample;
    sample.state = state;
    sample.reason = reason;
    sample.since = core::now_utc_micros();
    sample.window_start = sample.since;
    const auto previous = impl_->service->getChannelState(core::ChannelId{id}, ctx);
    if (core::is_ok(previous)) {
        const auto& old = core::value_of(previous).sample;
        if (old.source_epoch == impl_->epoch) {
            if (old.state == state) sample.since = old.since;
            sample.window_start = old.window_start;
            sample.last_success_time = old.last_success_time;
        }
    }
    if (state == ChannelRunState::Connected)
        sample.last_success_time = core::now_utc_micros();
    else
        sample.last_error = reason;
    sample.source_id = "ems-collector";
    sample.source_epoch = impl_->epoch;
    sample.sequence = ++impl_->sequence;
    sample.config_version = impl_->catalog->configVersion();
    auto updated = impl_->service->updateChannelState(core::ChannelId{id}, sample, ctx);
    return core::is_ok(updated) ? core::ok_status() : core::Status{core::error_of(updated)};
}

std::shared_ptr<const RuntimeCatalog> PointClient::catalog() const { return impl_->catalog; }
}  // namespace ems::rtdb
