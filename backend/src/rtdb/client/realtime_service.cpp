/**
 * @file realtime_service.cpp
 * @brief 类型化实时业务门面实现：样本校验、序号判断、新鲜度与能力运行条件。
 */
#include <rtdb/client/realtime_service.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ems::rtdb {
namespace {

constexpr const char* kSchemaVersion = "1";
constexpr std::uint32_t kMaxCasAttempts = 3;
constexpr core::UtcMicros kMaxClockSkewUs = 60LL * 1000 * 1000;  ///< 允许的源时钟超前
constexpr core::UtcMicros kHealthWindowUs = 5LL * 60 * 1000 * 1000;

core::Error readError(core::ErrorCode code, std::string message) {
    return core::make_error(code, core::WriteEffect::NotApplicable, std::move(message));
}

core::Error writeError(core::ErrorCode code, std::string message) {
    return core::make_error(code, core::WriteEffect::NotApplied, std::move(message));
}

bool parseU64(std::string_view text, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

bool parseI64(std::string_view text, std::int64_t& out) {
    if (text.empty()) return false;
    bool negative = false;
    std::size_t index = 0;
    if (text.front() == '-') {
        negative = true;
        index = 1;
    }
    if (index == text.size()) return false;
    std::uint64_t magnitude = 0;
    if (!parseU64(text.substr(index), magnitude)) return false;
    if (negative) {
        if (magnitude >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ULL) {
            return false;
        }
        out =
            magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ULL
                ? std::numeric_limits<std::int64_t>::min()
                : -static_cast<std::int64_t>(magnitude);
        return true;
    }
    if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return false;
    out = static_cast<std::int64_t>(magnitude);
    return true;
}

bool parseU32(std::string_view text, std::uint32_t& out) {
    std::uint64_t value = 0;
    if (!parseU64(text, value) || value > std::numeric_limits<std::uint32_t>::max()) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parseDouble(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

std::string encodeDouble(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

std::string encodeU64(std::uint64_t value) { return std::to_string(value); }

bool parseTime(const std::string& text, core::UtcMicros& out) {
    std::int64_t value = 0;
    if (!parseI64(text, value)) return false;
    out = value;
    return true;
}

bool parseQuality(const std::string& text, model::Quality& out) {
    std::uint32_t value = 0;
    if (!parseU32(text, value) || value > static_cast<std::uint32_t>(model::Quality::Disabled)) {
        return false;
    }
    out = static_cast<model::Quality>(value);
    return true;
}

bool parseBool(const std::string& text, bool& out) {
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    return false;
}

bool matchesType(const model::PointValue& value, model::ValueType type) {
    switch (type) {
        case model::ValueType::Bool:
            return std::holds_alternative<bool>(value);
        case model::ValueType::Int64:
            return std::holds_alternative<std::int64_t>(value);
        case model::ValueType::UInt64:
            return std::holds_alternative<std::uint64_t>(value);
        case model::ValueType::Double: {
            if (!std::holds_alternative<double>(value)) return false;
            return std::isfinite(std::get<double>(value));
        }
        case model::ValueType::String:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

core::Result<std::string> encodeValue(const model::PointValue& value, model::ValueType type) {
    if (!matchesType(value, type)) {
        return writeError(core::ErrorCode::InvalidArgument, "value does not match declared type");
    }
    switch (type) {
        case model::ValueType::Bool:
            return std::string(std::get<bool>(value) ? "true" : "false");
        case model::ValueType::Int64:
            return std::to_string(std::get<std::int64_t>(value));
        case model::ValueType::UInt64:
            return encodeU64(std::get<std::uint64_t>(value));
        case model::ValueType::Double:
            return encodeDouble(std::get<double>(value));
        case model::ValueType::String:
            return std::get<std::string>(value);
    }
    return writeError(core::ErrorCode::InvalidArgument, "unknown value type");
}

core::Result<model::PointValue> decodeValue(const std::string& text, model::ValueType type) {
    switch (type) {
        case model::ValueType::Bool: {
            bool value = false;
            if (!parseBool(text, value))
                return readError(core::ErrorCode::CorruptData, "bad bool value");
            return model::PointValue{value};
        }
        case model::ValueType::Int64: {
            std::int64_t value = 0;
            if (!parseI64(text, value))
                return readError(core::ErrorCode::CorruptData, "bad int64 value");
            return model::PointValue{value};
        }
        case model::ValueType::UInt64: {
            std::uint64_t value = 0;
            if (!parseU64(text, value))
                return readError(core::ErrorCode::CorruptData, "bad uint64 value");
            return model::PointValue{value};
        }
        case model::ValueType::Double: {
            double value = 0.0;
            if (!parseDouble(text, value))
                return readError(core::ErrorCode::CorruptData, "bad double value");
            return model::PointValue{value};
        }
        case model::ValueType::String:
            return model::PointValue{text};
    }
    return readError(core::ErrorCode::SchemaMismatch, "unknown value type");
}

std::optional<model::ValueType> parseValueType(const std::string& text) {
    if (text == "Bool") return model::ValueType::Bool;
    if (text == "Int64") return model::ValueType::Int64;
    if (text == "UInt64") return model::ValueType::UInt64;
    if (text == "Double") return model::ValueType::Double;
    if (text == "String") return model::ValueType::String;
    return std::nullopt;
}

std::string stringField(const Fields& fields, const char* name, std::string fallback = {}) {
    const auto it = fields.find(name);
    return it == fields.end() ? std::move(fallback) : it->second;
}

bool hasField(const Fields& fields, const char* name) { return fields.find(name) != fields.end(); }

void putTime(Fields& fields, const char* name, core::UtcMicros value) {
    if (core::is_valid_time(value)) fields[name] = std::to_string(value);
}

RecordKey pointKey(const PointAddress& address) {
    return RecordKey{address.scope, RecordKind::Point, address.device_path, address.code};
}

RecordKey scopeRuntimeKey(const Scope& scope) {
    return RecordKey{scope, RecordKind::AssetState, {}, {}};
}

RecordKey deviceStateKey(const RuntimeDevice& device) {
    return RecordKey{device.scope, RecordKind::AssetState, device.device_path, {}};
}

RecordKey channelStateKey(const RuntimeChannel& channel) {
    return RecordKey{channel.scope, RecordKind::ChannelState, {}, channel.code};
}

bool sameDeviceState(const DeviceStateSample& lhs, const DeviceStateSample& rhs) {
    return lhs.state == rhs.state && lhs.quality.code == rhs.quality.code &&
           lhs.quality.reason == rhs.quality.reason && lhs.since == rhs.since &&
           lhs.last_valid_time == rhs.last_valid_time && lhs.active_alarms == rhs.active_alarms &&
           lhs.config_version == rhs.config_version && lhs.source_id == rhs.source_id &&
           lhs.source_epoch == rhs.source_epoch && lhs.sequence == rhs.sequence;
}

std::string qualityName(model::Quality quality) { return model::to_string(quality); }

/// 计算新鲜度与时钟异常。
void evaluateFreshness(core::UtcMicros now, core::UtcMicros source_time,
                       core::UtcMicros ingest_time, std::uint32_t stale_after_ms, bool& stale,
                       bool& clock_anomaly, bool& time_degraded) {
    clock_anomaly = false;
    time_degraded = !core::is_valid_time(source_time);
    const core::UtcMicros baseline = core::is_valid_time(source_time) ? source_time : ingest_time;
    stale = false;
    if (!core::is_valid_time(baseline)) {
        stale = true;
        return;
    }
    if (baseline - now > kMaxClockSkewUs) {
        clock_anomaly = true;
        return;
    }
    const core::UtcMicros threshold =
        static_cast<core::UtcMicros>(stale_after_ms == 0 ? 60000 : stale_after_ms) * 1000;
    stale = now - baseline > threshold;
}

class PointRecordCodec {
   public:
    /**
     * @brief 组装完整测点记录（CAS 完整替换，不保留旧字段）。
     * @param[in] point 运行目录点定义。
     * @param[in] sample 采样。
     * @param[in] policy 已安装授权。
     * @param[in] now 当前时间。
     * @return 字段集合或错误。
     */
    static core::Result<Fields> encode(const RuntimePoint& point, const model::PointSample& sample,
                                       const SourcePolicy& policy, core::UtcMicros now) {
        Fields fields;
        fields["schema_version"] = kSchemaVersion;
        fields["point_id"] = point.point_id.value();
        fields["value_type"] = model::to_string(point.value_type);
        if (sample.value.has_value()) {
            auto encoded = encodeValue(*sample.value, point.value_type);
            if (!core::is_ok(encoded)) return core::error_of(encoded);
            fields["value"] = core::value_of(encoded);
        }
        fields["unit"] = point.unit;
        fields["quality"] = std::to_string(static_cast<int>(sample.quality.code));
        if (!sample.quality.reason.empty()) fields["quality_reason"] = sample.quality.reason;
        putTime(fields, "source_time", sample.source_time);
        putTime(fields, "ingest_time", sample.ingest_time);
        fields["source_id"] = sample.source_id;
        fields["source_epoch"] = encodeU64(sample.source_epoch.value);
        fields["sequence"] = encodeU64(sample.sequence);
        fields["config_version"] = sample.config_version;
        if (!sample.event_id.empty()) fields["event_id"] = sample.event_id;
        fields["accepted_source_id"] = policy.source_id;
        fields["accepted_source_epoch"] = encodeU64(policy.epoch.value);
        fields["accepted_config_version"] = policy.config_version;
        fields["write_enabled"] = policy.write_enabled ? "1" : "0";
        fields["updated_at"] = std::to_string(now);
        return fields;
    }

    /**
     * @brief 解析测点记录并计算派生状态。
     * @param[in] point 运行目录点定义。
     * @param[in] record 存储记录。
     * @param[in] catalog_version 当前目录版本。
     * @param[in] now 当前时间。
     * @return 类型化快照或错误。
     */
    static core::Result<PointSnapshot> decode(const RuntimePoint& point, const Record& record,
                                              const std::string& catalog_version,
                                              core::UtcMicros now) {
        if (record.revision.empty()) {
            return readError(core::ErrorCode::SchemaMismatch, "record revision is empty");
        }
        if (stringField(record.fields, "schema_version") != kSchemaVersion) {
            return readError(core::ErrorCode::SchemaMismatch, "unsupported point schema");
        }
        const auto stored_type = parseValueType(stringField(record.fields, "value_type"));
        if (!stored_type.has_value()) {
            return readError(core::ErrorCode::SchemaMismatch, "unknown stored value type");
        }

        PointSnapshot snapshot;
        snapshot.point_id = point.point_id;
        snapshot.point_type = point.point_type;
        snapshot.kind = point.kind;
        snapshot.value_type = point.value_type;
        snapshot.unit = stringField(record.fields, "unit", point.unit);
        snapshot.revision = record.revision;
        snapshot.source_id = stringField(record.fields, "source_id");
        snapshot.config_version = stringField(record.fields, "config_version");
        snapshot.event_id = stringField(record.fields, "event_id");

        std::uint64_t epoch = 0;
        std::uint64_t sequence = 0;
        if (!parseU64(stringField(record.fields, "source_epoch"), epoch) ||
            !parseU64(stringField(record.fields, "sequence"), sequence)) {
            return readError(core::ErrorCode::SchemaMismatch, "missing source epoch/sequence");
        }
        snapshot.source_epoch = core::SourceEpoch{epoch};
        snapshot.sequence = sequence;

        if (!parseQuality(stringField(record.fields, "quality"), snapshot.quality.code)) {
            return readError(core::ErrorCode::SchemaMismatch, "invalid quality code");
        }
        snapshot.quality.reason = stringField(record.fields, "quality_reason");

        if (hasField(record.fields, "value")) {
            auto decoded = decodeValue(stringField(record.fields, "value"), *stored_type);
            if (!core::is_ok(decoded)) return core::error_of(decoded);
            snapshot.value = core::value_of(decoded);
        }
        if (!parseTime(stringField(record.fields, "source_time"), snapshot.source_time)) {
            snapshot.source_time = core::kInvalidUtcMicros;
        }
        if (!parseTime(stringField(record.fields, "ingest_time"), snapshot.ingest_time)) {
            snapshot.ingest_time = core::kInvalidUtcMicros;
        }

        bool write_enabled = false;
        if (!parseBool(stringField(record.fields, "write_enabled"), write_enabled)) {
            return readError(core::ErrorCode::SchemaMismatch, "missing write_enabled");
        }
        snapshot.writable = write_enabled;

        std::uint64_t accepted_epoch = 0;
        if (!parseU64(stringField(record.fields, "accepted_source_epoch"), accepted_epoch)) {
            return readError(core::ErrorCode::SchemaMismatch, "missing accepted source epoch");
        }
        const std::string accepted_source = stringField(record.fields, "accepted_source_id");
        const std::string accepted_version = stringField(record.fields, "accepted_config_version");
        snapshot.source_authorized = snapshot.source_id == accepted_source &&
                                     snapshot.source_epoch.value == accepted_epoch &&
                                     snapshot.config_version == accepted_version;
        snapshot.config_current =
            snapshot.config_version == catalog_version && *stored_type == point.value_type;
        evaluateFreshness(now, snapshot.source_time, snapshot.ingest_time, point.stale_after_ms,
                          snapshot.stale, snapshot.clock_anomaly, snapshot.time_degraded);
        return snapshot;
    }
};

}  // namespace

const char* to_string(DeviceRunState state) noexcept {
    switch (state) {
        case DeviceRunState::Unconfigured:
            return "unconfigured";
        case DeviceRunState::Online:
            return "online";
        case DeviceRunState::Alarm:
            return "alarm";
        case DeviceRunState::Fault:
            return "fault";
        case DeviceRunState::Offline:
            return "offline";
    }
    return "unknown";
}

std::optional<DeviceRunState> parse_device_state(const std::string& text) {
    if (text == "unconfigured") return DeviceRunState::Unconfigured;
    if (text == "online") return DeviceRunState::Online;
    if (text == "alarm") return DeviceRunState::Alarm;
    if (text == "fault") return DeviceRunState::Fault;
    if (text == "offline") return DeviceRunState::Offline;
    return std::nullopt;
}

const char* to_string(ChannelRunState state) noexcept {
    switch (state) {
        case ChannelRunState::Disconnected:
            return "disconnected";
        case ChannelRunState::Connected:
            return "connected";
        case ChannelRunState::Degraded:
            return "degraded";
    }
    return "unknown";
}

std::optional<ChannelRunState> parse_channel_state(const std::string& text) {
    if (text == "disconnected") return ChannelRunState::Disconnected;
    if (text == "connected") return ChannelRunState::Connected;
    if (text == "degraded") return ChannelRunState::Degraded;
    return std::nullopt;
}

RealtimeService::RealtimeService(std::shared_ptr<IRealtimeStore> store,
                                 std::shared_ptr<const RuntimeCatalog> catalog, Clock clock)
    : store_(std::move(store)), catalog_(std::move(catalog)), clock_(std::move(clock)) {}

core::Result<PointSnapshot> RealtimeService::getPoint(const PointAddress& address,
                                                      const core::CallContext& ctx) {
    const RuntimePoint* point = catalog_->findPoint(address);
    if (point == nullptr) {
        return readError(core::ErrorCode::NotFound, "point is not in the runtime catalog");
    }
    if (core::has_expired(ctx)) {
        return readError(core::ErrorCode::DeadlineExceeded, "deadline exceeded before read");
    }
    auto record = store_->read(pointKey(address), ctx);
    if (!core::is_ok(record)) return core::error_of(record);
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    return PointRecordCodec::decode(*point, core::value_of(record), catalog_->configVersion(), now);
}

std::vector<core::Result<PointSnapshot>> RealtimeService::getPoints(
    const std::vector<PointAddress>& addresses, const core::CallContext& ctx) {
    std::vector<core::Result<PointSnapshot>> out(addresses.size(),
                                                 readError(core::ErrorCode::Internal, "unset"));
    std::vector<RecordKey> keys;
    std::vector<std::size_t> positions;
    keys.reserve(addresses.size());
    positions.reserve(addresses.size());
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        if (catalog_->findPoint(addresses[i]) == nullptr) {
            out[i] = readError(core::ErrorCode::NotFound, "point is not in the runtime catalog");
            continue;
        }
        keys.push_back(pointKey(addresses[i]));
        positions.push_back(i);
    }
    if (keys.empty()) return out;
    if (core::has_expired(ctx)) {
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            out[i] = readError(core::ErrorCode::DeadlineExceeded, "deadline exceeded before batch");
        }
        return out;
    }
    const auto records = store_->readMany(keys, ctx);
    if (records.size() != keys.size()) {
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            out[i] = readError(core::ErrorCode::Internal, "batch result size mismatch");
        }
        return out;
    }
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto& record = records[i];
        if (!core::is_ok(record)) {
            out[positions[i]] = core::error_of(record);
            continue;
        }
        const RuntimePoint* point = catalog_->findPoint(addresses[positions[i]]);
        out[positions[i]] = PointRecordCodec::decode(*point, core::value_of(record),
                                                     catalog_->configVersion(), now);
    }
    return out;
}

core::Result<SampleResult> RealtimeService::updatePoint(const PointAddress& address,
                                                        const model::PointSample& sample,
                                                        const core::CallContext& ctx) {
    const RuntimePoint* point = catalog_->findPoint(address);
    if (point == nullptr) {
        return writeError(core::ErrorCode::NotFound, "point is not in the runtime catalog");
    }
    if (!point->enabled) {
        return writeError(core::ErrorCode::NotReady, "point is disabled by configuration");
    }
    if (sample.value.has_value() && !matchesType(*sample.value, point->value_type)) {
        return writeError(core::ErrorCode::InvalidArgument,
                          "value does not match configured value type");
    }
    const auto now = clock_ ? clock_() : core::now_utc_micros();

    for (std::uint32_t attempt = 0; attempt < kMaxCasAttempts; ++attempt) {
        if (core::has_expired(ctx)) {
            return writeError(core::ErrorCode::DeadlineExceeded, "deadline exceeded during update");
        }
        auto existing = store_->read(pointKey(address), ctx);
        if (!core::is_ok(existing)) {
            const auto& error = core::error_of(existing);
            if (error.code == core::ErrorCode::NotFound) {
                return writeError(core::ErrorCode::NotReady,
                                  "point has no runtime record; create it first");
            }
            return error;
        }
        const Record& record = core::value_of(existing);
        if (record.revision.empty()) {
            return writeError(core::ErrorCode::SchemaMismatch, "record revision is empty");
        }
        std::uint64_t stored_epoch = 0;
        std::uint64_t stored_sequence = 0;
        if (!parseU64(stringField(record.fields, "source_epoch"), stored_epoch) ||
            !parseU64(stringField(record.fields, "sequence"), stored_sequence)) {
            return writeError(core::ErrorCode::SchemaMismatch,
                              "stored record has no source epoch/sequence");
        }
        bool write_enabled = false;
        std::uint64_t accepted_epoch = 0;
        if (!parseU64(stringField(record.fields, "accepted_source_epoch"), accepted_epoch) ||
            !parseBool(stringField(record.fields, "write_enabled"), write_enabled)) {
            return writeError(core::ErrorCode::SchemaMismatch,
                              "stored record has no installed authorization");
        }
        if (!write_enabled) {
            return writeError(core::ErrorCode::NotReady, "point write is not authorized");
        }
        const std::string accepted_source = stringField(record.fields, "accepted_source_id");
        const std::string accepted_version = stringField(record.fields, "accepted_config_version");
        if (sample.source_id != accepted_source || sample.source_epoch.value != accepted_epoch ||
            sample.config_version != accepted_version) {
            return writeError(core::ErrorCode::PermissionDenied,
                              "sample source or config version is not authorized");
        }

        if (sample.source_epoch.value < stored_epoch) return SampleResult::IgnoredOutOfOrder;
        if (sample.source_epoch.value == stored_epoch) {
            if (sample.sequence < stored_sequence) return SampleResult::IgnoredOutOfOrder;
            if (sample.sequence == stored_sequence) {
                auto stored =
                    PointRecordCodec::decode(*point, record, catalog_->configVersion(), now);
                if (!core::is_ok(stored)) return core::error_of(stored);
                const PointSnapshot& snapshot = core::value_of(stored);
                const bool same = snapshot.value == sample.value &&
                                  snapshot.quality.code == sample.quality.code &&
                                  snapshot.quality.reason == sample.quality.reason &&
                                  snapshot.source_time == sample.source_time &&
                                  snapshot.ingest_time == sample.ingest_time &&
                                  snapshot.event_id == sample.event_id;
                if (same) return SampleResult::Duplicate;
                return writeError(core::ErrorCode::Conflict,
                                  "same sequence with different content");
            }
        }

        SourcePolicy installed;
        installed.source_id = accepted_source;
        installed.epoch = core::SourceEpoch{accepted_epoch};
        installed.config_version = accepted_version;
        installed.write_enabled = write_enabled;
        auto fields = PointRecordCodec::encode(*point, sample, installed, now);
        if (!core::is_ok(fields)) return core::error_of(fields);
        WriteRequest request;
        request.key = pointKey(address);
        request.replacement = core::value_of(fields);
        request.expected = record.revision;
        auto written = store_->compareExchange(request, ctx);
        if (core::is_ok(written)) return SampleResult::Applied;
        const auto& error = core::error_of(written);
        if (error.code != core::ErrorCode::Conflict) return error;
        // CAS 冲突：重新读取后再次校验来源与序号。
    }
    return writeError(core::ErrorCode::Conflict, "point update conflicted after retries");
}

std::vector<core::Result<SampleResult>> RealtimeService::updatePoints(
    const std::vector<PointUpdate>& updates, const core::CallContext& ctx) {
    std::vector<core::Result<SampleResult>> out;
    out.reserve(updates.size());
    for (const auto& update : updates) {
        out.push_back(updatePoint(update.address, update.sample, ctx));
    }
    return out;
}

core::Result<Revision> RealtimeService::createPoint(const PointAddress& address,
                                                    const core::CallContext& ctx) {
    const RuntimePoint* point = catalog_->findPoint(address);
    if (point == nullptr) {
        return writeError(core::ErrorCode::NotFound, "point is not in the runtime catalog");
    }
    model::PointSample empty;
    empty.quality.code = model::Quality::Invalid;
    empty.source_id = point->source.source_id;
    empty.source_epoch = point->source.epoch;
    empty.config_version = point->source.config_version;
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    auto fields = PointRecordCodec::encode(*point, empty, point->source, now);
    if (!core::is_ok(fields)) return core::error_of(fields);
    if (core::has_expired(ctx)) {
        return writeError(core::ErrorCode::DeadlineExceeded, "deadline exceeded before create");
    }
    WriteRequest request;
    request.key = pointKey(address);
    request.replacement = core::value_of(fields);
    request.expected = std::nullopt;
    auto written = store_->compareExchange(request, ctx);
    if (!core::is_ok(written)) return core::error_of(written);
    return core::value_of(written).revision;
}

core::Result<Revision> RealtimeService::authorizePoint(const PointAddress& address,
                                                       const SourcePolicy& policy,
                                                       const core::CallContext& ctx) {
    const RuntimePoint* point = catalog_->findPoint(address);
    if (point == nullptr) {
        return writeError(core::ErrorCode::NotFound, "point is not in the runtime catalog");
    }
    SourcePolicy effective = policy;
    if (effective.config_version.empty()) effective.config_version = catalog_->configVersion();
    for (std::uint32_t attempt = 0; attempt < kMaxCasAttempts; ++attempt) {
        auto existing = store_->read(pointKey(address), ctx);
        if (!core::is_ok(existing)) {
            if (core::error_of(existing).code == core::ErrorCode::NotFound) {
                model::PointSample empty;
                empty.quality.code = model::Quality::Invalid;
                empty.source_id = effective.source_id;
                empty.source_epoch = effective.epoch;
                empty.config_version = effective.config_version;
                const auto now = clock_ ? clock_() : core::now_utc_micros();
                auto fields = PointRecordCodec::encode(*point, empty, effective, now);
                if (!core::is_ok(fields)) return core::error_of(fields);
                WriteRequest request;
                request.key = pointKey(address);
                request.replacement = core::value_of(fields);
                request.expected = std::nullopt;
                auto written = store_->compareExchange(request, ctx);
                if (core::is_ok(written)) return core::value_of(written).revision;
                if (core::error_of(written).code != core::ErrorCode::Conflict) {
                    return core::error_of(written);
                }
                continue;
            }
            return core::error_of(existing);
        }
        const Record& record = core::value_of(existing);
        Fields replacement = record.fields;
        replacement["accepted_source_id"] = effective.source_id;
        replacement["accepted_source_epoch"] = encodeU64(effective.epoch.value);
        replacement["accepted_config_version"] = effective.config_version;
        replacement["write_enabled"] = effective.write_enabled ? "1" : "0";
        replacement["updated_at"] = std::to_string(clock_ ? clock_() : core::now_utc_micros());
        WriteRequest request;
        request.key = pointKey(address);
        request.replacement = std::move(replacement);
        request.expected = record.revision;
        auto written = store_->compareExchange(request, ctx);
        if (core::is_ok(written)) return core::value_of(written).revision;
        if (core::error_of(written).code != core::ErrorCode::Conflict) {
            return core::error_of(written);
        }
    }
    return writeError(core::ErrorCode::Conflict, "authorization update conflicted after retries");
}

core::Result<DeviceSnapshot> RealtimeService::getDeviceState(const core::DeviceId& device_id,
                                                             const core::CallContext& ctx) {
    const RuntimeDevice* device = catalog_->findDevice(device_id);
    if (device == nullptr) {
        return readError(core::ErrorCode::NotFound, "device is not in the runtime catalog");
    }
    auto record = store_->read(deviceStateKey(*device), ctx);
    if (!core::is_ok(record)) return core::error_of(record);
    const Record& value = core::value_of(record);
    if (value.revision.empty() || stringField(value.fields, "schema_version") != kSchemaVersion) {
        return readError(core::ErrorCode::SchemaMismatch, "unsupported device state record");
    }
    DeviceSnapshot snapshot;
    snapshot.device_id = device_id;
    snapshot.revision = value.revision;
    auto state = parse_device_state(stringField(value.fields, "state"));
    if (!state.has_value()) {
        return readError(core::ErrorCode::SchemaMismatch, "unknown device state");
    }
    snapshot.sample.state = *state;
    if (!parseQuality(stringField(value.fields, "quality"), snapshot.sample.quality.code)) {
        return readError(core::ErrorCode::SchemaMismatch, "invalid device quality");
    }
    snapshot.sample.quality.reason = stringField(value.fields, "quality_reason");
    snapshot.sample.config_version = stringField(value.fields, "config_version");
    snapshot.sample.source_id = stringField(value.fields, "source_id");
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;
    std::uint32_t alarms = 0;
    if (!parseU64(stringField(value.fields, "source_epoch"), epoch) ||
        !parseU64(stringField(value.fields, "sequence"), sequence) ||
        !parseU32(stringField(value.fields, "active_alarms", "0"), alarms)) {
        return readError(core::ErrorCode::SchemaMismatch, "invalid device state counters");
    }
    snapshot.sample.source_epoch = core::SourceEpoch{epoch};
    snapshot.sample.sequence = sequence;
    snapshot.sample.active_alarms = alarms;
    if (!parseTime(stringField(value.fields, "since"), snapshot.sample.since)) {
        snapshot.sample.since = core::kInvalidUtcMicros;
    }
    if (!parseTime(stringField(value.fields, "last_valid_time"), snapshot.sample.last_valid_time)) {
        snapshot.sample.last_valid_time = core::kInvalidUtcMicros;
    }
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    bool anomaly = false;
    bool degraded = false;
    evaluateFreshness(now, snapshot.sample.last_valid_time, snapshot.sample.since,
                      catalog_->defaultStaleAfterMs(), snapshot.stale, anomaly, degraded);
    snapshot.config_current = snapshot.sample.config_version.empty() ||
                              snapshot.sample.config_version == catalog_->configVersion();
    return snapshot;
}

core::Result<Revision> RealtimeService::updateDeviceState(const core::DeviceId& device_id,
                                                          const DeviceStateSample& sample,
                                                          const core::CallContext& ctx) {
    const RuntimeDevice* device = catalog_->findDevice(device_id);
    if (device == nullptr) {
        return writeError(core::ErrorCode::NotFound, "device is not in the runtime catalog");
    }
    if (!device->enabled) {
        return writeError(core::ErrorCode::NotReady, "device is disabled by configuration");
    }
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    for (std::uint32_t attempt = 0; attempt < kMaxCasAttempts; ++attempt) {
        if (core::has_expired(ctx)) {
            return writeError(core::ErrorCode::DeadlineExceeded,
                              "deadline exceeded during device update");
        }
        auto existing = store_->read(deviceStateKey(*device), ctx);
        std::optional<Revision> expected;
        if (core::is_ok(existing)) {
            const Record& record = core::value_of(existing);
            std::uint64_t stored_epoch = 0;
            std::uint64_t stored_sequence = 0;
            if (!parseU64(stringField(record.fields, "source_epoch"), stored_epoch) ||
                !parseU64(stringField(record.fields, "sequence"), stored_sequence)) {
                return writeError(core::ErrorCode::SchemaMismatch,
                                  "stored device state has no epoch/sequence");
            }
            if (sample.source_epoch.value < stored_epoch) {
                return writeError(core::ErrorCode::Conflict, "device state source epoch is older");
            }
            if (sample.source_epoch.value == stored_epoch) {
                if (sample.sequence < stored_sequence) {
                    return writeError(core::ErrorCode::Conflict, "device state sequence is older");
                }
                if (sample.sequence == stored_sequence) {
                    auto current = getDeviceState(device_id, ctx);
                    if (core::is_ok(current) &&
                        sameDeviceState(core::value_of(current).sample, sample)) {
                        return core::value_of(current).revision;
                    }
                    return writeError(core::ErrorCode::Conflict,
                                      "same device sequence with different content");
                }
            }
            expected = record.revision;
        } else if (core::error_of(existing).code != core::ErrorCode::NotFound) {
            return core::error_of(existing);
        }

        Fields fields;
        fields["schema_version"] = kSchemaVersion;
        fields["device_id"] = device_id.value();
        fields["state"] = to_string(sample.state);
        fields["quality"] = std::to_string(static_cast<int>(sample.quality.code));
        if (!sample.quality.reason.empty()) fields["quality_reason"] = sample.quality.reason;
        putTime(fields, "since", sample.since);
        putTime(fields, "last_valid_time", sample.last_valid_time);
        fields["active_alarms"] = std::to_string(sample.active_alarms);
        fields["config_version"] = sample.config_version;
        fields["source_id"] = sample.source_id;
        fields["source_epoch"] = encodeU64(sample.source_epoch.value);
        fields["sequence"] = encodeU64(sample.sequence);
        fields["updated_at"] = std::to_string(now);

        WriteRequest request;
        request.key = deviceStateKey(*device);
        request.replacement = std::move(fields);
        request.expected = expected;
        auto written = store_->compareExchange(request, ctx);
        if (core::is_ok(written)) return core::value_of(written).revision;
        if (core::error_of(written).code != core::ErrorCode::Conflict) {
            return core::error_of(written);
        }
    }
    return writeError(core::ErrorCode::Conflict, "device state update conflicted after retries");
}

core::Result<ChannelSnapshot> RealtimeService::getChannelState(const core::ChannelId& channel_id,
                                                               const core::CallContext& ctx) {
    const RuntimeChannel* channel = catalog_->findChannel(channel_id);
    if (channel == nullptr) {
        return readError(core::ErrorCode::NotFound, "channel is not in the runtime catalog");
    }
    auto record = store_->read(channelStateKey(*channel), ctx);
    if (!core::is_ok(record)) return core::error_of(record);
    const Record& value = core::value_of(record);
    if (value.revision.empty() || stringField(value.fields, "schema_version") != kSchemaVersion) {
        return readError(core::ErrorCode::SchemaMismatch, "unsupported channel state record");
    }
    ChannelSnapshot snapshot;
    snapshot.channel_id = channel_id;
    snapshot.revision = value.revision;
    auto state = parse_channel_state(stringField(value.fields, "state"));
    if (!state.has_value()) {
        return readError(core::ErrorCode::SchemaMismatch, "unknown channel state");
    }
    snapshot.sample.state = *state;
    snapshot.sample.reason = stringField(value.fields, "reason");
    snapshot.sample.last_error = stringField(value.fields, "last_error");
    snapshot.sample.config_version = stringField(value.fields, "config_version");
    snapshot.sample.source_id = stringField(value.fields, "source_id");
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;
    if (!parseU64(stringField(value.fields, "source_epoch"), epoch) ||
        !parseU64(stringField(value.fields, "sequence"), sequence) ||
        !parseU64(stringField(value.fields, "requests", "0"), snapshot.sample.requests) ||
        !parseU64(stringField(value.fields, "successes", "0"), snapshot.sample.successes) ||
        !parseU64(stringField(value.fields, "timeouts", "0"), snapshot.sample.timeouts)) {
        return readError(core::ErrorCode::SchemaMismatch, "invalid channel counters");
    }
    snapshot.sample.source_epoch = core::SourceEpoch{epoch};
    snapshot.sample.sequence = sequence;
    if (!parseTime(stringField(value.fields, "since"), snapshot.sample.since)) {
        snapshot.sample.since = core::kInvalidUtcMicros;
    }
    if (!parseTime(stringField(value.fields, "window_start"), snapshot.sample.window_start)) {
        snapshot.sample.window_start = core::kInvalidUtcMicros;
    }
    if (!parseTime(stringField(value.fields, "last_success_time"),
                   snapshot.sample.last_success_time)) {
        snapshot.sample.last_success_time = core::kInvalidUtcMicros;
    }
    if (!parseDouble(stringField(value.fields, "avg_latency_ms", "0"),
                     snapshot.sample.average_latency_ms)) {
        snapshot.sample.average_latency_ms = 0.0;
    }
    if (!parseDouble(stringField(value.fields, "max_latency_ms", "0"),
                     snapshot.sample.max_latency_ms)) {
        snapshot.sample.max_latency_ms = 0.0;
    }
    snapshot.success_rate = snapshot.sample.requests == 0
                                ? 0.0
                                : static_cast<double>(snapshot.sample.successes) /
                                      static_cast<double>(snapshot.sample.requests);
    return snapshot;
}

core::Result<Revision> RealtimeService::updateChannelState(const core::ChannelId& channel_id,
                                                           const ChannelStateSample& sample,
                                                           const core::CallContext& ctx) {
    const RuntimeChannel* channel = catalog_->findChannel(channel_id);
    if (channel == nullptr) {
        return writeError(core::ErrorCode::NotFound, "channel is not in the runtime catalog");
    }
    if (!channel->enabled) {
        return writeError(core::ErrorCode::NotReady, "channel is disabled by configuration");
    }
    if (sample.successes > sample.requests || sample.timeouts > sample.requests) {
        return writeError(core::ErrorCode::InvalidArgument, "channel counters are inconsistent");
    }
    const auto now = clock_ ? clock_() : core::now_utc_micros();
    for (std::uint32_t attempt = 0; attempt < kMaxCasAttempts; ++attempt) {
        if (core::has_expired(ctx)) {
            return writeError(core::ErrorCode::DeadlineExceeded,
                              "deadline exceeded during channel update");
        }
        auto existing = store_->read(channelStateKey(*channel), ctx);
        std::optional<Revision> expected;
        if (core::is_ok(existing)) {
            const Record& record = core::value_of(existing);
            std::uint64_t stored_epoch = 0;
            std::uint64_t stored_sequence = 0;
            if (!parseU64(stringField(record.fields, "source_epoch"), stored_epoch) ||
                !parseU64(stringField(record.fields, "sequence"), stored_sequence)) {
                return writeError(core::ErrorCode::SchemaMismatch,
                                  "stored channel state has no epoch/sequence");
            }
            if (sample.source_epoch.value < stored_epoch) {
                return writeError(core::ErrorCode::Conflict, "channel state source epoch is older");
            }
            if (sample.source_epoch.value == stored_epoch) {
                if (sample.sequence < stored_sequence) {
                    return writeError(core::ErrorCode::Conflict, "channel state sequence is older");
                }
                // 同序号视为同一次窗口更新，直接返回现有版本，避免重试时重复累加。
                if (sample.sequence == stored_sequence) return record.revision;
            }
            expected = record.revision;
        } else if (core::error_of(existing).code != core::ErrorCode::NotFound) {
            return core::error_of(existing);
        }
        Fields fields;
        fields["schema_version"] = kSchemaVersion;
        fields["channel_id"] = channel_id.value();
        fields["channel_code"] = channel->code;
        fields["state"] = to_string(sample.state);
        if (!sample.reason.empty()) fields["reason"] = sample.reason;
        putTime(fields, "since", sample.since);
        putTime(fields, "window_start", sample.window_start);
        putTime(fields, "last_success_time", sample.last_success_time);
        if (!sample.last_error.empty()) fields["last_error"] = sample.last_error;
        fields["requests"] = encodeU64(sample.requests);
        fields["successes"] = encodeU64(sample.successes);
        fields["timeouts"] = encodeU64(sample.timeouts);
        fields["avg_latency_ms"] = encodeDouble(sample.average_latency_ms);
        fields["max_latency_ms"] = encodeDouble(sample.max_latency_ms);
        fields["config_version"] = sample.config_version;
        fields["source_id"] = sample.source_id;
        fields["source_epoch"] = encodeU64(sample.source_epoch.value);
        fields["sequence"] = encodeU64(sample.sequence);
        fields["updated_at"] = std::to_string(now);

        WriteRequest request;
        request.key = channelStateKey(*channel);
        request.replacement = std::move(fields);
        request.expected = expected;
        auto written = store_->compareExchange(request, ctx);
        if (core::is_ok(written)) return core::value_of(written).revision;
        if (core::error_of(written).code != core::ErrorCode::Conflict) {
            return core::error_of(written);
        }
    }
    return writeError(core::ErrorCode::Conflict, "channel state update conflicted after retries");
}

core::Result<ChannelSnapshot> RealtimeService::reportChannelHealth(
    const core::ChannelId& channel_id, const ChannelHealthDelta& delta,
    const core::CallContext& ctx) {
    if (delta.successes > delta.requests || delta.timeouts > delta.requests) {
        return writeError(core::ErrorCode::InvalidArgument,
                          "channel delta counters are inconsistent");
    }
    for (std::uint32_t attempt = 0; attempt < kMaxCasAttempts; ++attempt) {
        auto existing = getChannelState(channel_id, ctx);
        ChannelStateSample merged;
        bool create = false;
        if (!core::is_ok(existing)) {
            if (core::error_of(existing).code != core::ErrorCode::NotFound) {
                return core::error_of(existing);
            }
            create = true;
            merged.state = delta.state;
            merged.reason = delta.reason;
            merged.since = delta.observed_at;
            merged.window_start = delta.observed_at;
        } else {
            const ChannelSnapshot& current = core::value_of(existing);
            merged = current.sample;
            if (delta.source_epoch.value < merged.source_epoch.value) {
                return writeError(core::ErrorCode::Conflict, "channel health epoch is older");
            }
            if (delta.source_epoch.value == merged.source_epoch.value) {
                if (delta.sequence < merged.sequence) {
                    return writeError(core::ErrorCode::Conflict,
                                      "channel health sequence is older");
                }
                if (delta.sequence == merged.sequence) return current;
            }
            const bool new_window = !core::is_valid_time(merged.window_start) ||
                                    !core::is_valid_time(delta.observed_at) ||
                                    delta.observed_at - merged.window_start >= kHealthWindowUs;
            if (new_window) {
                merged.window_start = delta.observed_at;
                merged.requests = 0;
                merged.successes = 0;
                merged.timeouts = 0;
                merged.average_latency_ms = 0.0;
                merged.max_latency_ms = 0.0;
            }
            if (delta.state != merged.state) merged.since = delta.observed_at;
            merged.state = delta.state;
            if (!delta.reason.empty()) merged.reason = delta.reason;
        }

        const std::uint64_t old_requests = merged.requests;
        const std::uint64_t new_requests = old_requests + delta.requests;
        if (new_requests != 0) {
            merged.average_latency_ms =
                (merged.average_latency_ms * static_cast<double>(old_requests) +
                 delta.average_latency_ms * static_cast<double>(delta.requests)) /
                static_cast<double>(new_requests);
        }
        merged.requests = new_requests;
        merged.successes += delta.successes;
        merged.timeouts += delta.timeouts;
        merged.max_latency_ms = std::max(merged.max_latency_ms, delta.max_latency_ms);
        if (delta.successes > 0 && core::is_valid_time(delta.observed_at)) {
            merged.last_success_time = delta.observed_at;
        }
        if (!delta.last_error.empty()) merged.last_error = delta.last_error;
        merged.config_version = delta.config_version;
        merged.source_id = delta.source_id;
        merged.source_epoch = delta.source_epoch;
        merged.sequence = delta.sequence;

        if (create) {
            auto created = updateChannelState(channel_id, merged, ctx);
            if (core::is_ok(created)) {
                return getChannelState(channel_id, ctx);
            }
            if (core::error_of(created).code != core::ErrorCode::Conflict) {
                return core::error_of(created);
            }
            continue;
        }
        auto written = updateChannelState(channel_id, merged, ctx);
        if (core::is_ok(written)) return getChannelState(channel_id, ctx);
        if (core::error_of(written).code != core::ErrorCode::Conflict) {
            return core::error_of(written);
        }
    }
    return writeError(core::ErrorCode::Conflict, "channel health update conflicted after retries");
}

core::Result<ScopeRuntime> RealtimeService::getScopeRuntime(const core::CallContext& ctx) {
    auto record = store_->read(scopeRuntimeKey(catalog_->scope()), ctx);
    if (!core::is_ok(record)) return core::error_of(record);
    const Record& value = core::value_of(record);
    ScopeRuntime runtime;
    runtime.config_version = stringField(value.fields, "config_version");
    runtime.activation_revision = stringField(value.fields, "activation_revision");
    if (!parseTime(stringField(value.fields, "applied_at"), runtime.applied_at)) {
        runtime.applied_at = core::kInvalidUtcMicros;
    }
    if (runtime.config_version.empty()) {
        return readError(core::ErrorCode::SchemaMismatch, "scope runtime has no config version");
    }
    return runtime;
}

core::Result<Revision> RealtimeService::installScopeRuntime(const ScopeRuntime& runtime,
                                                            const std::optional<Revision>& expected,
                                                            const core::CallContext& ctx) {
    if (runtime.config_version.empty()) {
        return writeError(core::ErrorCode::InvalidArgument,
                          "scope runtime config version is empty");
    }
    Fields fields;
    fields["schema_version"] = kSchemaVersion;
    fields["config_version"] = runtime.config_version;
    fields["activation_revision"] = runtime.activation_revision;
    putTime(fields, "applied_at", runtime.applied_at);
    WriteRequest request;
    request.key = scopeRuntimeKey(catalog_->scope());
    request.replacement = std::move(fields);
    request.expected = expected;
    auto written = store_->compareExchange(request, ctx);
    if (!core::is_ok(written)) return core::error_of(written);
    return core::value_of(written).revision;
}

core::Result<CapabilityAvailability> RealtimeService::resolveCapability(
    const core::DeviceId& device_id, const std::string& capability_code,
    const core::CallContext& ctx) {
    const RuntimeDevice* device = catalog_->findDevice(device_id);
    if (device == nullptr) {
        return readError(core::ErrorCode::NotFound, "device is not in the runtime catalog");
    }
    CapabilityAvailability result;
    result.device_id = device_id;
    result.capability_code = capability_code;

    std::vector<std::string> reasons;
    const auto add_reason = [&reasons](const std::string& reason) {
        if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
            reasons.push_back(reason);
        }
    };

    if (!device->enabled) {
        add_reason("device_disabled");
        result.reasons = std::move(reasons);
        return result;
    }
    const RuntimeCapability* capability = catalog_->findCapability(device_id, capability_code);
    if (capability == nullptr) {
        add_reason("capability_not_configured");
        result.reasons = std::move(reasons);
        return result;
    }

    std::vector<core::PointId> required = capability->required_points;
    required.insert(required.end(), capability->required_feedbacks.begin(),
                    capability->required_feedbacks.end());

    std::vector<PointAddress> addresses;
    std::vector<core::PointId> address_points;
    addresses.reserve(required.size());
    address_points.reserve(required.size());
    for (const auto& point_id : required) {
        const RuntimePoint* point = catalog_->findPoint(point_id);
        if (point == nullptr) {
            CapabilityPointCheck check;
            check.point_id = point_id;
            check.reason = "point_not_configured";
            result.points.push_back(std::move(check));
            add_reason("point_not_configured");
            continue;
        }
        if (std::find(address_points.begin(), address_points.end(), point_id) !=
            address_points.end()) {
            continue;
        }
        addresses.push_back(point->address);
        address_points.push_back(point_id);
    }

    if (!addresses.empty()) {
        auto snapshots = getPoints(addresses, ctx);
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            CapabilityPointCheck check;
            check.point_id = address_points[i];
            check.code = addresses[i].code;
            const RuntimePoint* point = catalog_->findPoint(addresses[i]);
            if (!core::is_ok(snapshots[i])) {
                check.reason = core::error_of(snapshots[i]).code == core::ErrorCode::NotFound
                                   ? "no_sample"
                                   : "read_error";
            } else {
                const PointSnapshot& snapshot = core::value_of(snapshots[i]);
                if (!point->enabled) {
                    check.reason = "point_disabled";
                } else if (!snapshot.value.has_value()) {
                    check.reason = "no_value";
                } else if (snapshot.quality.code != model::Quality::Good) {
                    check.reason = "quality_" + qualityName(snapshot.quality.code);
                } else if (!snapshot.config_current) {
                    check.reason = "config_outdated";
                } else if (!snapshot.source_authorized) {
                    check.reason = "source_unauthorized";
                } else if (snapshot.stale) {
                    check.reason = "stale";
                } else {
                    check.ok = true;
                }
            }
            if (!check.ok) add_reason(check.reason);
            result.points.push_back(std::move(check));
        }
    }

    result.available = result.reasons.empty() &&
                       std::all_of(result.points.begin(), result.points.end(),
                                   [](const CapabilityPointCheck& check) { return check.ok; });
    if (!result.available && result.reasons.empty()) add_reason("capability_unavailable");
    result.reasons = std::move(reasons);
    return result;
}

}  // namespace ems::rtdb
