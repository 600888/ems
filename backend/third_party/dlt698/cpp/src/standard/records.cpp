#include <algorithm>
#include <dlt698/standard/records.hpp>

namespace dlt698::standard {
namespace {
Result<void> column_valid(std::uint16_t identifier, const model::Oad& column,
                          const DeviceLayout& layout) {
    const auto record = find_record(identifier);
    if (!record) return Error{ErrorCode::unsupported_tag, 0, "standard record"};
    if (find_object(identifier)->class_id == 7 && column == model::Oad{oi::event_source, 2, 0})
        return {};
    auto valid = validate_oad(column, layout);
    if (!valid) return valid;
    if (find_attribute(column)->record)
        return Error{ErrorCode::unsupported_service, 0, "nested record column"};
    return {};
}
}  // namespace

const RecordDefinition* find_record(std::uint16_t identifier) {
    static const RecordDefinition definitions[] = {
        {oi::daily_freeze,
         "日冻结",
         {oi::freeze_sequence, 2, 0},
         {oi::freeze_time, 2, 0},
         {{oi::freeze_sequence, 2, 0}, {oi::freeze_time, 2, 0}}},
        {oi::monthly_freeze,
         "月冻结",
         {oi::freeze_sequence, 2, 0},
         {oi::freeze_time, 2, 0},
         {{oi::freeze_sequence, 2, 0}, {oi::freeze_time, 2, 0}}},
        {oi::meter_power_down_event,
         "电能表掉电事件",
         {oi::event_sequence, 2, 0},
         {oi::event_start_time, 2, 0},
         {{oi::event_sequence, 2, 0},
          {oi::event_start_time, 2, 0},
          {oi::event_end_time, 2, 0},
          {oi::event_source, 2, 0}}},
        {oi::terminal_initialization_event,
         "终端初始化事件",
         {oi::event_sequence, 2, 0},
         {oi::event_start_time, 2, 0},
         {{oi::event_sequence, 2, 0},
          {oi::event_start_time, 2, 0},
          {oi::event_end_time, 2, 0},
          {oi::event_source, 2, 0}}}};
    const auto found =
        std::find_if(std::begin(definitions), std::end(definitions),
                     [identifier](const auto& definition) { return definition.oi == identifier; });
    return found == std::end(definitions) ? nullptr : found;
}

Result<void> validate_record_time(const model::DateTimeS& time) {
    const auto& t = time.value;
    const auto year = (unsigned(t[0]) << 8) | t[1];
    static constexpr unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!year || year > 9999 || !t[2] || t[2] > 12 || !t[3] || t[4] > 23 || t[5] > 59 || t[6] > 59)
        return Error{ErrorCode::invalid_value, 0, "record complete time"};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (t[3] > days[t[2] - 1] + (t[2] == 2 && leap ? 1u : 0u))
        return Error{ErrorCode::invalid_value, 0, "record calendar date"};
    return {};
}

Result<void> validate_record_query(const protocol::apdu::GetRecord& query,
                                   const DeviceLayout& layout, const Limits& limits) {
    if (!find_record(query.attribute.oi))
        return Error{ErrorCode::unsupported_tag, 0, "standard record"};
    auto attribute = validate_oad(query.attribute, layout);
    if (!attribute) return attribute;
    if ((query.attribute.attribute & 31) != 2 || query.attribute.index)
        return Error{ErrorCode::invalid_value, 0, "record attribute"};
    if (query.columns.size() > limits.max_elements)
        return Error{ErrorCode::resource_limit, 0, "record columns"};
    std::vector<model::Oad> seen;
    for (const auto& descriptor : query.columns) {
        const auto column = std::get_if<model::Oad>(&descriptor);
        if (!column) return Error{ErrorCode::unsupported_service, 0, "record ROAD column"};
        if (std::find(seen.begin(), seen.end(), *column) != seen.end())
            return Error{ErrorCode::invalid_value, 0, "duplicate record column"};
        auto valid = column_valid(query.attribute.oi, *column, layout);
        if (!valid) return valid;
        seen.push_back(*column);
    }
    // 保留 RSD 的所有线上分支；具体选择业务由后端负责，codec 限制深度和字节规模。
    auto wire =
        protocol::apdu::encode_get(protocol::apdu::GetRecordRequest{0, false, {query}, {}}, limits);
    if (!wire) return wire.error();
    return {};
}

Result<protocol::apdu::GetRecord> make_record_query(std::uint16_t identifier, model::Rsd rows,
                                                    std::vector<model::Oad> columns,
                                                    const DeviceLayout& layout,
                                                    const Limits& limits) {
    protocol::apdu::GetRecord query{{identifier, 2, 0}, std::move(rows), {}};
    for (const auto& column : columns) query.columns.emplace_back(column);
    auto valid = validate_record_query(query, layout, limits);
    if (!valid) return valid.error();
    return query;
}

Result<protocol::apdu::GetRecord> record_at(std::uint16_t identifier, model::DateTimeS time,
                                            std::vector<model::Oad> columns,
                                            const DeviceLayout& layout, const Limits& limits) {
    const auto definition = find_record(identifier);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard record"};
    auto valid = validate_record_time(time);
    if (!valid) return valid.error();
    return make_record_query(identifier, model::Selector1{definition->time, time},
                             std::move(columns), layout, limits);
}

Result<protocol::apdu::GetRecord> record_between(std::uint16_t identifier, model::DateTimeS begin,
                                                 model::DateTimeS end,
                                                 std::vector<model::Oad> columns,
                                                 const DeviceLayout& layout, const Limits& limits) {
    const auto definition = find_record(identifier);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard record"};
    auto valid = validate_record_time(begin);
    if (!valid) return valid.error();
    valid = validate_record_time(end);
    if (!valid) return valid.error();
    if (!(begin.value < end.value)) return Error{ErrorCode::invalid_value, 0, "record time range"};
    // 表 30 明确前闭后开，NULL 间隔表示区间内全部记录，不误用 DateTimeS 作间隔。
    return make_record_query(identifier,
                             model::Selector2{definition->time, begin, end, model::Null{}},
                             std::move(columns), layout, limits);
}

Result<protocol::apdu::GetRecord> record_sequences(std::uint16_t identifier, std::uint32_t begin,
                                                   std::uint32_t end,
                                                   std::vector<model::Oad> columns,
                                                   const DeviceLayout& layout,
                                                   const Limits& limits) {
    const auto definition = find_record(identifier);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard record"};
    if (begin >= end) return Error{ErrorCode::invalid_value, 0, "record sequence range"};
    return make_record_query(identifier,
                             model::Selector2{definition->sequence, model::UInt32{begin},
                                              model::UInt32{end}, model::Null{}},
                             std::move(columns), layout, limits);
}

Result<void> validate_record_cell(std::uint16_t identifier, const model::Oad& column,
                                  const model::Data& value, const DeviceLayout& layout,
                                  const Limits& limits) {
    auto valid = column_valid(identifier, column, layout);
    if (!valid) return valid;
    if (column == model::Oad{oi::event_source, 2, 0}) {
        if (value.type() != model::DataType::null)
            return Error{ErrorCode::invalid_value, 0, "event NULL source"};
        auto wire = codec::encode_data(value, limits);
        if (!wire) return wire.error();
        return {};
    }
    return validate_value(column, value, layout, limits);
}

Result<void> validate_record_result(const protocol::apdu::GetRecord& query,
                                    const protocol::apdu::RecordResult& result,
                                    const DeviceLayout& layout, const Limits& limits) {
    auto valid = validate_record_query(query, layout, limits);
    if (!valid) return valid;
    if (!(result.attribute == query.attribute) ||
        (!query.columns.empty() && !(result.columns == query.columns)))
        return Error{ErrorCode::invalid_value, 0, "record response descriptors"};
    auto header = query;
    header.columns = result.columns;
    valid = validate_record_query(header, layout, limits);
    if (!valid) return valid;
    if (const auto rows = std::get_if<std::vector<protocol::apdu::RecordRow>>(&result.result)) {
        if (rows->size() > limits.max_elements)
            return Error{ErrorCode::resource_limit, 0, "record response rows"};
        for (const auto& row : *rows) {
            if (row.size() != result.columns.size() || row.empty())
                return Error{ErrorCode::invalid_length, 0, "record row width"};
            for (std::size_t i = 0; i < row.size(); ++i) {
                valid = validate_record_cell(query.attribute.oi,
                                             std::get<model::Oad>(result.columns[i]), row[i],
                                             layout, limits);
                if (!valid) return valid;
            }
        }
    }
    auto wire = protocol::apdu::encode_get(
        protocol::apdu::GetRecordResponse{0, false, {result}, {}}, limits);
    if (!wire) return wire.error();
    return {};
}
}  // namespace dlt698::standard
