#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ems::database {

enum class ValueType { Numeric, Integer, Boolean, Text };
using SampleValue = std::variant<std::monostate, double, std::int64_t, bool, std::string>;

struct SeriesMapping {
    std::string point_id;
    ValueType value_type{ValueType::Numeric};
    std::string series_key;
};

struct Sample {
    std::string event_id;
    std::string point_id;
    std::string scope_kind;
    std::string scope_id;
    std::string config_version;
    std::string series_key;
    std::string source_id;
    std::string source_epoch;
    std::string sequence;
    std::string quality_reason;
    std::string time_reason;
    std::string payload_sha256;
    std::int64_t source_time_us{0};
    std::int64_t ingest_time_us{0};
    std::int32_t quality{0};
    std::uint8_t time_origin{0};
    ValueType value_type{ValueType::Numeric};
    SampleValue value;
};

struct QueryCursor {
    std::int64_t source_time_us{0};
    std::string point_id;
    std::string event_id;
    std::string fingerprint;
    std::int64_t expires_at_us{0};
};

struct HistoryQuery {
    std::string scope_kind;
    std::string scope_id;
    std::vector<std::string> point_ids;
    std::int64_t start_us{0};
    std::int64_t end_us{0};
    std::uint32_t max_rows{0};
    std::optional<QueryCursor> cursor;
};

}  // namespace ems::database
