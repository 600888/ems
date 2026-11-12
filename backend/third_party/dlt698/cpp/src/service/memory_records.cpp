#include <algorithm>
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/service/memory_records.hpp>
#include <mutex>

namespace dlt698::service {
struct MemoryRecords::Impl {
    std::uint16_t oi;
    std::vector<model::Oad> columns;
    standard::DeviceLayout layout;
    Limits limits;
    RecordLimits record_limits;
    std::size_t sequence_index = 0;
    std::size_t time_index = 0;
    std::mutex mutex;
    std::shared_ptr<const std::vector<protocol::apdu::RecordRow>> rows =
        std::make_shared<const std::vector<protocol::apdu::RecordRow>>();
};

MemoryRecords::MemoryRecords(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MemoryRecords::~MemoryRecords() = default;

Result<std::shared_ptr<MemoryRecords>> MemoryRecords::create(std::uint16_t identifier,
                                                             std::vector<model::Oad> columns,
                                                             const standard::DeviceLayout& layout,
                                                             const Limits& limits,
                                                             const RecordLimits& budget) {
    const auto definition = standard::find_record(identifier);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "memory record object"};
    if (!budget.max_rows || !budget.max_result_rows || !budget.max_columns ||
        !budget.max_snapshot_bytes)
        return Error{ErrorCode::invalid_value, 0, "memory record budget"};
    if (columns.empty()) columns = definition->base_columns;
    if (columns.size() > budget.max_columns)
        return Error{ErrorCode::resource_limit, 0, "memory record columns"};
    auto query =
        standard::make_record_query(identifier, model::SelectAll{}, columns, layout, limits);
    if (!query) return query.error();
    for (const auto& column : definition->base_columns)
        if (std::find(columns.begin(), columns.end(), column) == columns.end())
            return Error{ErrorCode::invalid_value, 0, "missing record key/base column"};
    auto impl = std::make_unique<Impl>();
    impl->oi = identifier;
    impl->layout = layout;
    impl->limits = limits;
    impl->record_limits = budget;
    impl->sequence_index = static_cast<std::size_t>(
        std::find(columns.begin(), columns.end(), definition->sequence) - columns.begin());
    impl->time_index = static_cast<std::size_t>(
        std::find(columns.begin(), columns.end(), definition->time) - columns.begin());
    impl->columns = std::move(columns);
    return std::shared_ptr<MemoryRecords>(new MemoryRecords(std::move(impl)));
}

Result<void> MemoryRecords::replace_rows(std::vector<protocol::apdu::RecordRow> rows) {
    if (rows.size() > impl_->record_limits.max_rows)
        return Error{ErrorCode::resource_limit, 0, "memory record rows"};
    std::size_t bytes = 0;
    std::optional<std::uint32_t> previous;
    for (const auto& row : rows) {
        if (row.size() != impl_->columns.size())
            return Error{ErrorCode::invalid_length, 0, "memory record row width"};
        for (std::size_t i = 0; i < row.size(); ++i) {
            auto valid = standard::validate_record_cell(impl_->oi, impl_->columns[i], row[i],
                                                        impl_->layout, impl_->limits);
            if (!valid) return valid;
        }
        auto valid_time =
            standard::validate_record_time(row[impl_->time_index].as<model::DateTimeS>());
        if (!valid_time) return valid_time;
        const auto sequence = row[impl_->sequence_index].as<model::UInt32>().value;
        if (previous && sequence <= *previous)
            return Error{ErrorCode::invalid_value, 0, "record sequence order"};
        previous = sequence;
        // 整行使用一个 Data 树计数，避免逐单元检查让总节点规模绕过限制；减法避免累加溢出。
        auto encoded = codec::encode_data(model::Structure{row}, impl_->limits);
        if (!encoded) return encoded.error();
        if (encoded.value().size() > impl_->record_limits.max_snapshot_bytes - bytes)
            return Error{ErrorCode::resource_limit, 0, "memory record snapshot bytes"};
        bytes += encoded.value().size();
    }
    auto snapshot = std::make_shared<const std::vector<protocol::apdu::RecordRow>>(std::move(rows));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->rows = std::move(snapshot);
    return {};
}

ObjectValue MemoryRecords::read(const model::Oad& attribute) {
    return attribute.oi == impl_->oi && (attribute.attribute & 31) == 2 ? std::uint8_t{5}
                                                                        : std::uint8_t{4};
}

protocol::apdu::RecordResult MemoryRecords::read_record(const protocol::apdu::GetRecord& query) {
    auto fail = [&](std::uint8_t dar) {
        return protocol::apdu::RecordResult{query.attribute, query.columns, dar};
    };
    if (query.attribute.oi != impl_->oi) return fail(4);
    auto valid = standard::validate_record_query(query, impl_->layout, impl_->limits);
    if (!valid) {
        if (valid.error().code == ErrorCode::unsupported_service ||
            valid.error().code == ErrorCode::resource_limit)
            return fail(3);
        if (valid.error().code == ErrorCode::unsupported_tag) return fail(4);
        return fail(8);
    }
    if (query.columns.size() > impl_->record_limits.max_columns) return fail(3);
    std::vector<std::size_t> selected;
    model::Rcsd columns;
    if (query.columns.empty()) {
        for (std::size_t i = 0; i < impl_->columns.size(); ++i) {
            selected.push_back(i);
            columns.emplace_back(impl_->columns[i]);
        }
    } else {
        columns = query.columns;
        for (const auto& descriptor : columns) {
            const auto& column = std::get<model::Oad>(descriptor);
            const auto found = std::find(impl_->columns.begin(), impl_->columns.end(), column);
            if (found == impl_->columns.end()) return fail(4);
            selected.push_back(static_cast<std::size_t>(found - impl_->columns.begin()));
        }
    }
    std::shared_ptr<const std::vector<protocol::apdu::RecordRow>> snapshot;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        snapshot = impl_->rows;
    }
    // 快照引用保活旧数据，筛选与编码在锁外进行；不会阻塞并发替换或改变分块中已交付的结果。
    auto key_index = [&](const model::Oad& attribute) -> std::optional<std::size_t> {
        const auto definition = standard::find_record(impl_->oi);
        if (attribute == definition->sequence) return impl_->sequence_index;
        if (attribute == definition->time) return impl_->time_index;
        return {};
    };
    auto key_valid = [&](const model::Data& key, std::size_t index) {
        if (index == impl_->sequence_index) return key.type() == model::DataType::uint32;
        return key.type() == model::DataType::date_time_s &&
               bool(standard::validate_record_time(key.as<model::DateTimeS>()));
    };
    auto less = [](const model::Data& a, const model::Data& b) {
        if (a.type() == model::DataType::uint32)
            return a.as<model::UInt32>().value < b.as<model::UInt32>().value;
        return a.as<model::DateTimeS>().value < b.as<model::DateTimeS>().value;
    };
    std::function<bool(const protocol::apdu::RecordRow&)> match = [](const auto&) { return true; };
    std::optional<std::size_t> previous_index;
    if (const auto selector = std::get_if<model::Selector1>(&query.rows)) {
        const auto index = key_index(selector->attribute);
        if (!index || !key_valid(selector->value, *index)) return fail(8);
        match = [selector, index](const auto& row) { return row[*index] == selector->value; };
    } else if (const auto range = std::get_if<model::Selector2>(&query.rows)) {
        const auto index = key_index(range->attribute);
        if (range->interval.type() != model::DataType::null) return fail(3);
        if (!index || !key_valid(range->begin, *index) || !key_valid(range->end, *index) ||
            !less(range->begin, range->end))
            return fail(8);
        match = [range, index, less](const auto& row) {
            return !less(row[*index], range->begin) && less(row[*index], range->end);
        };
    } else if (const auto previous = std::get_if<model::Selector9>(&query.rows)) {
        if (!previous->previous) return fail(8);
        previous_index = previous->previous <= snapshot->size()
                             ? snapshot->size() - previous->previous
                             : snapshot->size();
    } else if (!std::holds_alternative<model::SelectAll>(query.rows))
        return fail(3);
    std::vector<protocol::apdu::RecordRow> result;
    for (std::size_t i = 0; i < snapshot->size(); ++i) {
        const auto& row = (*snapshot)[i];
        if ((previous_index && *previous_index != i) || !match(row)) continue;
        if (result.size() >= impl_->record_limits.max_result_rows) return fail(3);
        protocol::apdu::RecordRow projected;
        projected.reserve(selected.size());
        for (const auto index : selected) projected.push_back(row[index]);
        result.push_back(std::move(projected));
    }
    protocol::apdu::RecordResult response{query.attribute, std::move(columns), std::move(result)};
    valid = standard::validate_record_result(query, response, impl_->layout, impl_->limits);
    if (!valid) return fail(valid.error().code == ErrorCode::resource_limit ? 3 : 7);
    return response;
}
}  // namespace dlt698::service
