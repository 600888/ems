#include "storage.h"

#include <common/codec/event_codec.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace ems::storage {
namespace {

using nlohmann::json;

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotate_right(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::string sha256(const std::string& input) {
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56) bytes.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto base = offset + i * 4;
            words[i] = (static_cast<std::uint32_t>(bytes[base]) << 24) |
                       (static_cast<std::uint32_t>(bytes[base + 1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[base + 2]) << 8) | bytes[base + 3];
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotate_right(words[i - 15], 7) ^ rotate_right(words[i - 15], 18) ^
                            (words[i - 15] >> 3);
            const auto s1 = rotate_right(words[i - 2], 17) ^ rotate_right(words[i - 2], 19) ^
                            (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        auto working = state;
        for (std::size_t i = 0; i < 64; ++i) {
            const auto s1 = rotate_right(working[4], 6) ^ rotate_right(working[4], 11) ^
                            rotate_right(working[4], 25);
            const auto choose = (working[4] & working[5]) ^ (~working[4] & working[6]);
            const auto first = working[7] + s1 + choose + kRoundConstants[i] + words[i];
            const auto s0 = rotate_right(working[0], 2) ^ rotate_right(working[0], 13) ^
                            rotate_right(working[0], 22);
            const auto majority =
                (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
            const auto second = s0 + majority;
            for (std::size_t j = 7; j > 0; --j) working[j] = working[j - 1];
            working[4] += first;
            working[0] = first + second;
        }
        for (std::size_t i = 0; i < state.size(); ++i) state[i] += working[i];
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (auto word : state) output << std::setw(8) << word;
    return output.str();
}

core::Error invalid(const std::string& message) {
    return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable,
                            message);
}

bool decimal_u64(const std::string& value) {
    if (value.empty() || value.size() > 20) return false;
    for (const auto ch : value) {
        if (ch < '0' || ch > '9') return false;
    }
    try {
        (void)std::stoull(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool valid_id(const std::string& value, std::size_t limit = 64) {
    return !value.empty() && value.size() <= limit;
}

bool valid_series_key(const std::string& value) {
    if (value.empty() || value.size() > 96) return false;
    return std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               ch == '-' || ch == '_';
    });
}

core::Result<std::string> required_string(const json& payload, const char* field,
                                          std::size_t limit = 64) {
    if (!payload.contains(field) || !payload[field].is_string()) return invalid(field);
    const auto value = payload[field].get<std::string>();
    if (!valid_id(value, limit)) return invalid(field);
    return value;
}

core::Result<ValueType> value_type_of(const std::string& name) {
    if (name == "numeric") return ValueType::Numeric;
    if (name == "integer") return ValueType::Integer;
    if (name == "boolean") return ValueType::Boolean;
    if (name == "text") return ValueType::Text;
    return invalid("value_type");
}

bool deterministic(core::ErrorCode code) {
    return code == core::ErrorCode::InvalidArgument || code == core::ErrorCode::SchemaMismatch ||
           code == core::ErrorCode::CorruptData || code == core::ErrorCode::Conflict;
}

}  // namespace

core::Result<Sample> parse_sample(const messaging::EventEnvelope& event,
                                  const ConsumerOptions& options) {
    if (event.event_type != messaging::EventType::Telemetry || event.schema_version != "1" ||
        !valid_id(event.event_id.value()) || !event.station_id.has_value() ||
        !valid_id(event.station_id->value()) || !decimal_u64(event.config_version) ||
        !valid_id(event.producer_id) || event.payload_json.size() > options.max_payload_bytes) {
        return invalid("telemetry envelope");
    }
    if (event.unit_id && !valid_id(event.unit_id->value())) return invalid("unit_id");
    const auto payload = json::parse(event.payload_json, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) return invalid("payload JSON");
    auto point_id = required_string(payload, "point_id");
    if (!core::is_ok(point_id)) return core::error_of(point_id);
    auto source_id = required_string(payload, "source_id");
    if (!core::is_ok(source_id)) return core::error_of(source_id);
    auto source_epoch = required_string(payload, "source_epoch", 20);
    if (!core::is_ok(source_epoch)) return core::error_of(source_epoch);
    auto sequence = required_string(payload, "sequence", 20);
    if (!core::is_ok(sequence)) return core::error_of(sequence);
    if (!decimal_u64(core::value_of(source_epoch)) || !decimal_u64(core::value_of(sequence))) {
        return invalid("source sequence");
    }
    if (!payload.contains("value_type") || !payload["value_type"].is_string()) {
        return invalid("value_type");
    }
    auto type = value_type_of(payload["value_type"].get<std::string>());
    if (!core::is_ok(type)) return core::error_of(type);
    if (!payload.contains("quality") || !payload["quality"].is_number_integer()) {
        return invalid("quality");
    }
    const auto quality = payload["quality"].get<std::int64_t>();
    if (quality < 0 || quality > std::numeric_limits<std::int32_t>::max()) {
        return invalid("quality range");
    }
    if (!payload.contains("value")) return invalid("value");
    Sample sample;
    sample.event_id = event.event_id.value();
    sample.point_id = core::value_of(point_id);
    sample.scope_kind = event.unit_id ? "unit" : "station";
    sample.scope_id = event.unit_id ? event.unit_id->value() : event.station_id->value();
    sample.config_version = event.config_version;
    sample.source_id = core::value_of(source_id);
    sample.source_epoch = core::value_of(source_epoch);
    sample.sequence = core::value_of(sequence);
    sample.quality = static_cast<std::int32_t>(quality);
    sample.value_type = core::value_of(type);
    sample.ingest_time_us = event.ingest_time;
    const bool source_time_missing = event.event_time == core::kInvalidUtcMicros;
    sample.source_time_us = source_time_missing ? event.ingest_time : event.event_time;
    sample.time_origin = source_time_missing ? 1 : 0;
    sample.time_reason = source_time_missing ? "source_time_missing" : "";
    if (sample.ingest_time_us <= 0 || sample.source_time_us < options.min_source_time_us ||
        sample.source_time_us <= 0) {
        return invalid("sample time");
    }
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    if (sample.source_time_us > now_us + options.max_future_skew_us) {
        return invalid("future sample time");
    }
    if (payload.contains("quality_reason")) {
        if (!payload["quality_reason"].is_string()) return invalid("quality_reason");
        sample.quality_reason = payload["quality_reason"].get<std::string>();
        if (sample.quality_reason.size() > 64) return invalid("quality_reason length");
    }
    if (payload.contains("time_reason")) {
        if (!payload["time_reason"].is_string()) return invalid("time_reason");
        sample.time_reason = payload["time_reason"].get<std::string>();
        if (sample.time_reason.size() > 64) return invalid("time_reason length");
    }
    if (source_time_missing && sample.time_reason.empty()) return invalid("time_reason required");
    const auto& value = payload["value"];
    if (value.is_null()) {
        if (sample.quality_reason.empty()) return invalid("null value requires quality_reason");
    } else if (sample.value_type == ValueType::Numeric && value.is_number()) {
        const auto numeric = value.get<double>();
        if (!std::isfinite(numeric)) return invalid("numeric value");
        sample.value = numeric;
    } else if (sample.value_type == ValueType::Integer && value.is_string()) {
        const auto text = value.get<std::string>();
        try {
            std::size_t consumed = 0;
            const auto parsed = std::stoll(text, &consumed);
            if (consumed != text.size()) return invalid("integer value");
            sample.value = static_cast<std::int64_t>(parsed);
        } catch (...) {
            return invalid("integer value");
        }
    } else if (sample.value_type == ValueType::Boolean && value.is_boolean()) {
        sample.value = value.get<bool>();
    } else if (sample.value_type == ValueType::Text && value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.size() > 1024) return invalid("text value length");
        sample.value = text;
    } else {
        return invalid("value does not match value_type");
    }
    auto canonical = codec::encode_event(event);
    if (!core::is_ok(canonical)) return core::error_of(canonical);
    sample.payload_sha256 = sha256(core::value_of(canonical));
    return sample;
}

HistoryConsumer::HistoryConsumer(IHistoryFeed& feed, database::IDatabase& database,
                                 database::IHistoryCatalogRepository& repository,
                                 database::IHistoryEventRepository& events,
                                 ITimeSeriesStore& series, std::string scope,
                                 ConsumerOptions options)
    : feed_(feed),
      database_(database),
      repository_(repository),
      events_(events),
      series_(series),
      scope_(std::move(scope)),
      options_(options) {}

core::Status HistoryConsumer::process(const messaging::Delivery& delivery,
                                      const core::CallContext& ctx, BatchReport& report) {
    auto sample = parse_sample(delivery.event, options_);
    auto quarantine = [&](const std::string& reason) -> core::Status {
        auto encoded = codec::encode_event(delivery.event);
        const std::string raw = !delivery.raw_payload.empty() ? delivery.raw_payload
                                : core::is_ok(encoded)        ? core::value_of(encoded)
                                                              : delivery.event.payload_json;
        QuarantineRecord record{delivery.transport_cursor.value, scope_,
                                delivery.event.event_id.value(), reason,
                                raw.substr(0, options_.max_payload_bytes)};
        auto begun = database_.begin(ctx);
        if (!core::is_ok(begun)) return core::error_of(begun);
        auto& transaction = *std::get<0>(begun);
        auto saved = events_.quarantine(transaction, record, ctx);
        if (!core::is_ok(saved)) return core::error_of(saved);
        auto committed = transaction.commit(ctx);
        if (!core::is_ok(committed)) return core::error_of(committed);
        ++report.quarantined;
        return core::ok_status();
    };
    if (!core::is_ok(sample)) return quarantine(core::error_of(sample).message);
    auto value = core::value_of(sample);
    if (value.scope_id != scope_ ||
        (!options_.scope_kind.empty() && value.scope_kind != options_.scope_kind)) {
        return quarantine("telemetry scope mismatch");
    }
    auto begun = database_.begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    auto transaction = std::move(std::get<0>(begun));
    auto snapshot = repository_.find_point(*transaction, value.scope_kind, value.scope_id,
                                           value.config_version, value.point_id, ctx);
    if (!core::is_ok(snapshot)) {
        const auto& error = core::error_of(snapshot);
        transaction.reset();
        return deterministic(error.code) ? quarantine(error.message) : core::Status{error};
    }
    const auto& point = core::value_of(snapshot);
    if (point.value_type != value.value_type || !point.enabled ||
        point.point_id != value.point_id || point.scope_kind != value.scope_kind ||
        point.scope_id != value.scope_id || point.config_version != value.config_version ||
        !valid_series_key(point.series_key)) {
        transaction.reset();
        return quarantine("point snapshot mismatch");
    }
    value.series_key = point.series_key;
    auto registered = events_.register_event(*transaction, value, ctx);
    if (!core::is_ok(registered)) {
        const auto& error = core::error_of(registered);
        transaction.reset();
        return error.code == core::ErrorCode::Conflict ? quarantine("event identity conflict")
                                                       : core::Status{error};
    }
    auto committed = transaction->commit(ctx);
    if (!core::is_ok(committed)) return core::error_of(committed);
    transaction.reset();
    auto written = series_.write(value, ctx);
    if (!core::is_ok(written)) return core::error_of(written);
    ++report.written;
    return core::ok_status();
}

core::Result<BatchReport> HistoryConsumer::consume_batch(const core::CallContext& ctx) {
    if (options_.max_items == 0 || options_.max_bytes == 0 || scope_.empty() ||
        (!options_.scope_kind.empty() && options_.scope_kind != "station" &&
         options_.scope_kind != "unit")) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "consumer configuration");
    }
    auto deliveries = feed_.read(options_.max_items, options_.max_bytes, ctx);
    if (!core::is_ok(deliveries)) return core::error_of(deliveries);
    BatchReport report;
    for (const auto& delivery : core::value_of(deliveries)) {
        ++report.received;
        auto handled = process(delivery, ctx, report);
        if (!core::is_ok(handled)) ++report.dropped;
    }
    return report;
}

HistoryQueryService::HistoryQueryService(database::IDatabase& database,
                                         database::IHistoryCatalogRepository& repository,
                                         ITimeSeriesReader& reader)
    : database_(database), repository_(repository), reader_(reader) {}

core::Result<HistoryPage> HistoryQueryService::query(
    const HistoryQuery& request, const std::vector<std::string>& authorized_scopes,
    const core::CallContext& ctx) {
    constexpr std::int64_t kMaxSpanUs = 31LL * 24 * 60 * 60 * 1000 * 1000;
    constexpr std::int64_t kCursorLifetimeUs = 10LL * 60 * 1000 * 1000;
    if ((request.scope_kind != "station" && request.scope_kind != "unit") ||
        !valid_id(request.scope_id) || request.point_ids.empty() || request.point_ids.size() > 64 ||
        request.max_rows == 0 || request.max_rows > 1000 || request.start_us <= 0 ||
        request.end_us <= request.start_us || request.end_us - request.start_us > kMaxSpanUs) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "history query bounds");
    }
    const auto scope_key = request.scope_kind + ":" + request.scope_id;
    if (std::find(authorized_scopes.begin(), authorized_scopes.end(), scope_key) ==
        authorized_scopes.end()) {
        return core::make_error(core::ErrorCode::PermissionDenied, core::WriteEffect::NotApplicable,
                                "scope not authorized");
    }
    std::set<std::string> point_set;
    for (const auto& point_id : request.point_ids) {
        if (!valid_id(point_id) || !point_set.insert(point_id).second) {
            return core::make_error(core::ErrorCode::InvalidArgument,
                                    core::WriteEffect::NotApplicable, "point IDs");
        }
    }
    std::string fingerprint_input =
        scope_key + "|" + std::to_string(request.start_us) + "|" + std::to_string(request.end_us);
    for (const auto& point_id : point_set) fingerprint_input += "|" + point_id;
    const auto fingerprint = sha256(fingerprint_input);
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    if (request.cursor &&
        (request.cursor->fingerprint != fingerprint || request.cursor->expires_at_us <= now_us ||
         request.cursor->source_time_us < request.start_us ||
         request.cursor->source_time_us >= request.end_us ||
         !point_set.count(request.cursor->point_id) || request.cursor->event_id.empty())) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "history cursor");
    }
    auto begun = database_.begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    auto transaction = std::move(std::get<0>(begun));
    std::vector<SeriesMapping> mappings;
    mappings.reserve(point_set.size());
    for (const auto& point_id : point_set) {
        auto mapping = repository_.find_series(*transaction, point_id, ctx);
        if (!core::is_ok(mapping)) return core::error_of(mapping);
        if (core::value_of(mapping).point_id != point_id ||
            !valid_series_key(core::value_of(mapping).series_key)) {
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                    "invalid series mapping");
        }
        mappings.push_back(core::value_of(mapping));
    }
    auto closed = transaction->rollback(ctx);
    if (!core::is_ok(closed)) return core::error_of(closed);
    transaction.reset();
    auto scanned = reader_.scan(request, mappings, request.max_rows + 1, ctx);
    if (!core::is_ok(scanned)) return core::error_of(scanned);
    const auto& samples = core::value_of(scanned);
    if (samples.size() > static_cast<std::size_t>(request.max_rows) + 1) {
        return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                "unbounded history reader");
    }
    begun = database_.begin(ctx);
    if (!core::is_ok(begun)) return core::error_of(begun);
    transaction = std::move(std::get<0>(begun));
    HistoryPage page;
    std::map<std::pair<std::string, std::string>, PointSnapshot> snapshots;
    std::optional<std::tuple<std::int64_t, std::string, std::string>> previous;
    for (const auto& sample : samples) {
        const auto key = std::make_tuple(sample.source_time_us, sample.point_id, sample.event_id);
        if (sample.scope_kind != request.scope_kind || sample.scope_id != request.scope_id ||
            !point_set.count(sample.point_id) || sample.source_time_us < request.start_us ||
            sample.source_time_us >= request.end_us || (previous && key <= *previous) ||
            (request.cursor &&
             key <= std::make_tuple(request.cursor->source_time_us, request.cursor->point_id,
                                    request.cursor->event_id))) {
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                    "history reader scope/order");
        }
        previous = key;
        if (page.rows.size() == request.max_rows) break;
        const auto snapshot_key = std::make_pair(sample.config_version, sample.point_id);
        auto found = snapshots.find(snapshot_key);
        if (found == snapshots.end()) {
            auto resolved = repository_.find_point(*transaction, sample.scope_kind, sample.scope_id,
                                                   sample.config_version, sample.point_id, ctx);
            if (!core::is_ok(resolved)) return core::error_of(resolved);
            found = snapshots.emplace(snapshot_key, core::value_of(resolved)).first;
        }
        if (found->second.value_type != sample.value_type ||
            found->second.scope_kind != sample.scope_kind ||
            found->second.scope_id != sample.scope_id ||
            found->second.config_version != sample.config_version ||
            found->second.point_id != sample.point_id) {
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplicable,
                                    "history metadata mismatch");
        }
        page.rows.push_back({sample, found->second});
    }
    if (samples.size() > request.max_rows && !page.rows.empty()) {
        const auto& last = page.rows.back().sample;
        page.next = QueryCursor{last.source_time_us, last.point_id, last.event_id, fingerprint,
                                now_us + kCursorLifetimeUs};
    }
    closed = transaction->rollback(ctx);
    if (!core::is_ok(closed)) return core::error_of(closed);
    return page;
}

}  // namespace ems::storage
