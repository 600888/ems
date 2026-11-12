#include <dlt698/protocol/apdu/apdu.hpp>

namespace dlt698::protocol::apdu {
Result<Apdu> decode_apdu(ByteView bytes, const Limits& limits) {
    if (bytes.empty()) return Error{ErrorCode::need_more_data, 0, "APDU service"};
    if (bytes[0] == 8 || bytes[0] == 0x88 || bytes[0] == 9 || bytes[0] == 0x89 ||
        ((bytes[0] == 6 || bytes[0] == 7 || bytes[0] == 0x86 || bytes[0] == 0x87) &&
         bytes.size() > 1 && bytes[1] == 3)) {
        auto value = decode_advanced(bytes, limits);
        if (!value) return value.error();
        return std::visit([](auto&& v) -> Apdu { return std::move(v); }, std::move(value).value());
    }
    if (bytes[0] == 6 || bytes[0] == 7 || bytes[0] == 0x86 || bytes[0] == 0x87) {
        auto value = decode_mutation(bytes, limits);
        if (!value) return value.error();
        return std::visit([](auto&& v) -> Apdu { return std::move(v); }, std::move(value).value());
    }
    if (bytes[0] == 5 || bytes[0] == 0x85) {
        auto value = decode_get(bytes, limits);
        if (!value) return value.error();
        return std::visit([](auto&& v) -> Apdu { return std::move(v); }, std::move(value).value());
    }
    auto value = decode_connection(bytes, limits);
    if (!value) return value.error();
    return std::visit([](auto&& v) -> Apdu { return std::move(v); }, std::move(value).value());
}

Result<Bytes> encode_apdu(const Apdu& message, const Limits& limits) {
    return std::visit(
        [&](const auto& v) -> Result<Bytes> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, GetRequest> || std::is_same_v<T, GetResponse> ||
                          std::is_same_v<T, GetRecordRequest> ||
                          std::is_same_v<T, GetRecordResponse> ||
                          std::is_same_v<T, GetNextRequest> || std::is_same_v<T, GetNextResponse> ||
                          std::is_same_v<T, GetMd5Request> || std::is_same_v<T, GetMd5Response>)
                return encode_get(GetApdu{v}, limits);
            else if constexpr (std::is_same_v<T, SetRequest> || std::is_same_v<T, SetResponse> ||
                               std::is_same_v<T, ActionRequest> ||
                               std::is_same_v<T, ActionResponse>)
                return encode_mutation(MutationApdu{v}, limits);
            else if constexpr (std::is_same_v<T, SetThenGetRequest> ||
                               std::is_same_v<T, SetThenGetResponse> ||
                               std::is_same_v<T, ActionThenGetRequest> ||
                               std::is_same_v<T, ActionThenGetResponse> ||
                               std::is_same_v<T, ReportNotification> ||
                               std::is_same_v<T, ReportResponse> ||
                               std::is_same_v<T, ProxyRequest> || std::is_same_v<T, ProxyResponse>)
                return encode_advanced(AdvancedApdu{v}, limits);
            else
                return encode_connection(ConnectionApdu{v}, limits);
        },
        message);
}
}  // namespace dlt698::protocol::apdu
