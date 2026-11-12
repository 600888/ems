#include <algorithm>
#include <dlt698/protocol/apdu/get_block.hpp>

namespace dlt698::protocol::apdu {
Result<std::vector<GetNextResponse>> GetBlockTransfer::split(GetSnapshot snapshot,
                                                             std::size_t target,
                                                             const Limits& limits) {
    if (!target) return Error{ErrorCode::invalid_value, 0, "GET block target"};
    std::vector<GetNextResponse> blocks;
    auto result = std::visit(
        [&](auto& response) -> Result<void> {
            using T = std::decay_t<decltype(response)>;
            auto& units = [&]() -> auto& {
                if constexpr (std::is_same_v<T, GetResponse>)
                    return response.attributes;
                else
                    return response.records;
            }();
            using Units = std::decay_t<decltype(units)>;
            if (units.empty() || units.size() > limits.max_elements)
                return Error{ErrorCode::resource_limit, 0, "GET snapshot units"};
            GetNextResponse current{response.piid_acd, false, 0, Units{}, response.time_tag};
            auto flush = [&]() -> Result<void> {
                if (!encode_get(GetApdu{current}, limits))
                    return Error{ErrorCode::resource_limit, 0, "GET indivisible unit"};
                blocks.push_back(std::move(current));
                if (blocks.size() >= 65536)
                    return Error{ErrorCode::resource_limit, 0, "GET block sequence exhausted"};
                current = GetNextResponse{response.piid_acd, false,
                                          static_cast<std::uint16_t>(blocks.size()), Units{},
                                          response.time_tag};
                return {};
            };
            auto append_unit = [&](typename Units::value_type unit) -> Result<void> {
                auto& group = std::get<Units>(current.result);
                group.push_back(std::move(unit));
                auto encoded = encode_get(GetApdu{current}, limits);
                if ((!encoded || encoded.value().size() > target) && group.size() > 1) {
                    auto last = std::move(group.back());
                    group.pop_back();
                    auto valid = flush();
                    if (!valid) return valid;
                    std::get<Units>(current.result).push_back(std::move(last));
                    encoded = encode_get(GetApdu{current}, limits);
                }
                if (!encoded) return encoded.error();
                return {};
            };
            bool unique = true;
            if constexpr (std::is_same_v<T, GetRecordResponse>) {
                for (std::size_t i = 0; i < units.size(); ++i)
                    for (std::size_t j = 0; j < i; ++j)
                        unique = unique && !(units[i].attribute == units[j].attribute);
            }
            for (auto& unit : units) {
                if constexpr (std::is_same_v<T, GetRecordResponse>) {
                    auto rows = std::get_if<std::vector<RecordRow>>(&unit.result);
                    if (unique && rows && !rows->empty()) {
                        if (rows->size() > limits.max_elements)
                            return Error{ErrorCode::resource_limit, 0, "GET snapshot rows"};
                        // 同一表只在行边界拆分；每块重复精确 OAD/RCSD，单行绝不拆开。
                        auto& group = std::get<Units>(current.result);
                        group.push_back({unit.attribute, unit.columns, std::vector<RecordRow>{}});
                        for (auto& row : *rows) {
                            auto& partial = std::get<std::vector<RecordRow>>(group.back().result);
                            partial.push_back(std::move(row));
                            auto encoded = encode_get(GetApdu{current}, limits);
                            if (!encoded || encoded.value().size() > target) {
                                auto last = std::move(partial.back());
                                partial.pop_back();
                                if (partial.empty()) group.pop_back();
                                if (!group.empty()) {
                                    auto valid = flush();
                                    if (!valid) return valid;
                                }
                                std::get<Units>(current.result)
                                    .push_back({unit.attribute, unit.columns,
                                                std::vector<RecordRow>{std::move(last)}});
                                encoded = encode_get(GetApdu{current}, limits);
                            }
                            if (!encoded) return encoded.error();
                        }
                        continue;
                    }
                }
                auto valid = append_unit(std::move(unit));
                if (!valid) return valid;
            }
            current.last = true;
            // 跟随上报只附在末块，不能在分块时丢弃或在每个块重复交付。
            current.follow_report = std::move(response.follow_report);
            const auto encoded = encode_get(GetApdu{current}, limits);
            if (!encoded) return encoded.error();
            blocks.push_back(std::move(current));
            return {};
        },
        snapshot);
    if (!result) return result.error();
    return blocks;
}

GetBlockTransfer::GetBlockTransfer(std::uint8_t piid, bool records, Limits limits,
                                   bool merge_record_rows)
    : piid_(piid), records_(records), merge_record_rows_(merge_record_rows), limits_(limits) {}

Result<std::optional<GetSnapshot>> GetBlockTransfer::accept(const GetNextResponse& block) {
    if (finished_ || (block.piid_acd & 0xbf) != piid_ || block.block != next_)
        return Error{ErrorCode::invalid_value, 0, "GET block PIID/order"};
    if (const auto dar = std::get_if<std::uint8_t>(&block.result)) {
        finished_ = true;
        return Error{ErrorCode::remote_error, 0, "GET block DAR", *dar};
    }
    if (block.result.index() != (records_ ? 2u : 1u))
        return Error{ErrorCode::invalid_value, 0, "GET block result kind"};
    auto encoded = encode_get(GetApdu{block}, limits_);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > limits_.max_data_bytes - bytes_)
        return Error{ErrorCode::resource_limit, 0, "GET aggregate bytes"};
    if (!block.last && next_ == 65535)
        return Error{ErrorCode::resource_limit, 0, "GET block sequence exhausted"};
    if (records_) {
        const auto& incoming = std::get<std::vector<RecordResult>>(block.result);
        if (incoming.empty()) return Error{ErrorCode::invalid_value, 0, "GET empty block"};
        // 在临时结果上验证再提交，列头/类型/行数错误不会留下半个合并结果。
        auto combined = records_data_;
        for (const auto& record : incoming) {
            if (merge_record_rows_ && !combined.empty() &&
                combined.back().attribute == record.attribute) {
                auto prior = std::get_if<std::vector<RecordRow>>(&combined.back().result);
                const auto rows = std::get_if<std::vector<RecordRow>>(&record.result);
                if (!prior || !rows || !(combined.back().columns == record.columns))
                    return Error{ErrorCode::invalid_value, 0,
                                 "GET record continuation header/result"};
                const auto maximum = record.columns.empty()
                                         ? limits_.max_elements
                                         : limits_.max_elements / record.columns.size();
                if (prior->size() > maximum || rows->size() > maximum - prior->size())
                    return Error{ErrorCode::resource_limit, 0, "GET aggregate rows/cells"};
                prior->insert(prior->end(), rows->begin(), rows->end());
            } else {
                if (combined.size() >= limits_.max_elements)
                    return Error{ErrorCode::resource_limit, 0, "GET aggregate units"};
                combined.push_back(record);
            }
        }
        records_data_ = std::move(combined);
    } else {
        const auto& incoming = std::get<std::vector<AttributeResult>>(block.result);
        if (incoming.empty() || incoming.size() > limits_.max_elements - attributes_.size())
            return Error{ErrorCode::resource_limit, 0, "GET aggregate units"};
        attributes_.insert(attributes_.end(), incoming.begin(), incoming.end());
    }
    // 累计实际传输字节（含重复表头），防止小块绕过总量限制。
    bytes_ += encoded.value().size();
    ++next_;
    if (!block.last) return std::optional<GetSnapshot>{};
    finished_ = true;
    if (records_)
        return std::optional<GetSnapshot>{GetRecordResponse{
            block.piid_acd, false, std::move(records_data_), block.time_tag, block.follow_report}};
    return std::optional<GetSnapshot>{GetResponse{block.piid_acd, false, std::move(attributes_),
                                                  block.time_tag, block.follow_report}};
}
}  // namespace dlt698::protocol::apdu
