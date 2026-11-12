#include <algorithm>
#include <dlt698/standard/capabilities.hpp>

namespace dlt698::standard {
namespace {
template <std::size_t N>
bool has_bit(const std::array<std::uint8_t, N>& bytes, unsigned index) {
    return index < N * 8 && (bytes[index / 8] & (0x80u >> (index % 8)));
}

Support feature(const Capabilities& capabilities, unsigned index) {
    if (!capabilities.negotiated) return Support::unknown;
    const auto& bits = capabilities.negotiated->function;
    bool information = false;
    for (unsigned i = 0; i < 30; ++i) information = information || has_bit(bits, i);
    if (!information) return Support::unknown;
    return has_bit(bits, index) ? Support::yes : Support::no;
}
}  // namespace

Result<Capabilities> capabilities_from_connect(const protocol::apdu::ConnectResponse& response) {
    if (response.result || !has_bit(response.parameters.protocol, 0)) {
        Error error{ErrorCode::association_failed, 0, "CONNECT capability result"};
        if (response.result) error.remote_code = response.result;
        return error;
    }
    return Capabilities{response.parameters};
}

Support service_support(const Capabilities& capabilities, ReadService service) {
    if (!capabilities.negotiated) return Support::unknown;
    const auto index = static_cast<unsigned>(service);
    if (index != 1 && index != 2 && index != 3 && index != 6) return Support::unknown;
    return has_bit(capabilities.negotiated->protocol, index) ? Support::yes : Support::no;
}

Support point_support(const Capabilities& capabilities, const model::Oad& attribute) {
    const auto object = find_object(attribute.oi);
    const auto definition = find_attribute(attribute);
    if (!object || !definition) return Support::unknown;
    unsigned index = 128;
    if (object->class_id == 1) {
        const auto unit = find_attribute({attribute.oi, 2, 0})->scaling->unit;
        index = unit == 33 ? 0 : unit == 35 ? 2 : 3;
        if ((attribute.oi & 0xfff0) == oi::reverse_active_energy &&
            feature(capabilities, 1) == Support::no)
            return Support::no;
    } else if (object->class_id == 2) {
        const auto unit =
            find_attribute({attribute.oi, 2, 0})->element_definition->fields[0].scaling->unit;
        index = unit == 28 ? 4 : unit == 32 ? 5 : 6;
    } else if (object->class_id == 5)
        index = 13;
    else if (object->class_id == 7)
        index = 16;
    else if (attribute.oi == oi::voltage_distortion || attribute.oi == oi::current_distortion)
        index = 14;
    else if (attribute.oi == oi::internal_temperature)
        index = 18;
    else if (attribute.oi == oi::operating_status || attribute.oi == oi::follow_report_status)
        index = 19;
    else if (attribute.oi == oi::current_active_demand)
        index = 4;
    else if (attribute.oi == oi::current_reactive_demand)
        index = 5;
    else if (attribute.oi == oi::current_apparent_demand)
        index = 6;
    if (index == 128) return Support::unknown;
    // 功能位仅是业务提示；显式筛选才能删除 no，缺信息时不能删除整张点表。
    if (definition->layout == ArrayLayout::total_tariffs && attribute.index >= 2 &&
        feature(capabilities, 7) == Support::no)
        return Support::no;
    return feature(capabilities, index);
}

std::vector<CandidatePoint> candidate_points(const Capabilities& capabilities,
                                             const std::vector<model::Oad>& attributes,
                                             bool discard_negative) {
    std::vector<CandidatePoint> result;
    result.reserve(attributes.size());
    for (const auto& attribute : attributes) {
        const auto hint = point_support(capabilities, attribute);
        if (!discard_negative || hint != Support::no) result.push_back({attribute, hint});
    }
    return result;
}

Result<std::vector<ReadBatch>> plan_reads(const Capabilities& capabilities,
                                          const std::vector<model::Oad>& attributes,
                                          std::size_t batch_size, const Limits& limits) {
    if (attributes.empty() || !batch_size)
        return Error{ErrorCode::invalid_value, 0, "read plan size"};
    if (attributes.size() > limits.max_elements || batch_size > limits.max_elements)
        return Error{ErrorCode::resource_limit, 0, "read plan points"};
    const bool list = service_support(capabilities, ReadService::list) == Support::yes;
    if (!list && service_support(capabilities, ReadService::normal) == Support::no)
        return Error{ErrorCode::unsupported_service, 0, "normal/list read unavailable"};
    const auto width = list ? batch_size : std::size_t{1};
    std::vector<ReadBatch> result;
    for (std::size_t first = 0; first < attributes.size();) {
        const auto count = std::min(width, attributes.size() - first);
        ReadBatch batch{list, {attributes.begin() + first, attributes.begin() + first + count}};
        auto bytes = protocol::apdu::encode_get(
            protocol::apdu::GetRequest{0, list, batch.attributes, {}}, limits);
        if (!bytes) return bytes.error();
        result.push_back(std::move(batch));
        first += count;
    }
    return result;
}

Result<void> require_record_service(const Capabilities& capabilities) {
    if (service_support(capabilities, ReadService::record) == Support::no)
        return Error{ErrorCode::unsupported_service, 0, "record read unavailable"};
    return {};
}
}  // namespace dlt698::standard
