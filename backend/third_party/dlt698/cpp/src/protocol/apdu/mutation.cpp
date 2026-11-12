#include <dlt698/protocol/apdu/mutation.hpp>

#include "detail.hpp"

namespace dlt698::protocol::apdu {
namespace {
model::Omd read_method(Reader& r) {
    return {static_cast<std::uint16_t>(r.be(2, "OI")), r.u8("method"), r.u8("mode")};
}

void write_method(Writer& w, const model::Omd& method) {
    w.be(method.oi, 2);
    w.u8(method.method);
    w.u8(method.mode);
}

template <class T>
T read_message(Reader& r, std::uint8_t piid, bool list, std::size_t count, const Limits& limits) {
    T message;
    message.list = list;
    if constexpr (std::is_same_v<T, SetRequest> || std::is_same_v<T, ActionRequest>)
        message.piid = piid;
    else
        message.piid_acd = piid;
    // 每项至少四字节描述符及一个值/结果字节；先检查最小需求，避免伪数量分配。
    if (count > r.remaining() / 5)
        throw DecodeFailure({ErrorCode::need_more_data, r.position(), "mutation items"});
    for (std::size_t i = 0; i < count; ++i) {
        if constexpr (std::is_same_v<T, SetRequest>) {
            const auto attribute = codec::read_oad(r);
            message.attributes.push_back({attribute, codec::read_data(r, limits)});
        } else if constexpr (std::is_same_v<T, SetResponse>) {
            const auto attribute = codec::read_oad(r);
            message.attributes.push_back({attribute, r.u8("DAR")});
        } else if constexpr (std::is_same_v<T, ActionRequest>) {
            const auto method = read_method(r);
            message.methods.push_back({method, codec::read_data(r, limits)});
        } else {
            const auto method = read_method(r);
            const auto dar = r.u8("DAR");
            std::optional<model::Data> data;
            // ACTION 返回数据是 OPTIONAL，不是 GET 的 DAR/Data CHOICE。
            if (detail::present(r, "action Data presence")) data = codec::read_data(r, limits);
            message.methods.push_back({method, dar, std::move(data)});
        }
    }
    if constexpr (std::is_same_v<T, SetResponse> || std::is_same_v<T, ActionResponse>)
        message.follow_report = detail::read_follow(r, limits);
    message.time_tag = detail::read_time_tag(r);
    r.finish();
    return message;
}
}  // namespace

Result<MutationApdu> decode_mutation(ByteView bytes, const Limits& limits) {
    if (bytes.size() > limits.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "APDU limit"};
    try {
        Reader r(bytes);
        const auto service = r.u8("mutation service");
        if (service != 6 && service != 7 && service != 0x86 && service != 0x87)
            return Error{ErrorCode::unsupported_service, 0, "mutation service"};
        const auto choice = r.u8("mutation choice");
        if (choice != 1 && choice != 2)
            return Error{ErrorCode::unsupported_service, 1, "then-get/variant"};
        const auto piid = r.u8("PIID");
        if (service < 0x80) detail::piid(piid, 2);
        const auto count = choice == 2 ? codec::read_length(r, limits.max_elements) : 1;
        if (!count || count > limits.max_elements)
            return Error{ErrorCode::invalid_length, r.position(), "mutation count"};
        if (service == 6)
            return MutationApdu{read_message<SetRequest>(r, piid, choice == 2, count, limits)};
        if (service == 7)
            return MutationApdu{read_message<ActionRequest>(r, piid, choice == 2, count, limits)};
        if (service == 0x86)
            return MutationApdu{read_message<SetResponse>(r, piid, choice == 2, count, limits)};
        return MutationApdu{read_message<ActionResponse>(r, piid, choice == 2, count, limits)};
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_mutation(const MutationApdu& message, const Limits& limits) {
    try {
        Writer w(limits.max_data_bytes);
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                constexpr bool request =
                    std::is_same_v<T, SetRequest> || std::is_same_v<T, ActionRequest>;
                constexpr bool set =
                    std::is_same_v<T, SetRequest> || std::is_same_v<T, SetResponse>;
                std::size_t count = 0;
                if constexpr (set)
                    count = v.attributes.size();
                else
                    count = v.methods.size();
                if (!count || count > limits.max_elements || (!v.list && count != 1))
                    throw DecodeFailure({ErrorCode::invalid_length, 0, "mutation count"});
                w.u8(static_cast<std::uint8_t>((set ? 6 : 7) + (request ? 0 : 0x80)));
                w.u8(v.list ? 2 : 1);
                if constexpr (request) {
                    detail::piid(v.piid, 2);
                    w.u8(v.piid);
                } else
                    w.u8(v.piid_acd);
                if (v.list) codec::write_length(w, count);
                if constexpr (set) {
                    for (const auto& item : v.attributes) {
                        codec::write_oad(w, item.attribute);
                        if constexpr (request)
                            codec::write_data(w, item.value, limits);
                        else
                            w.u8(item.dar);
                    }
                } else {
                    for (const auto& item : v.methods) {
                        write_method(w, item.method);
                        if constexpr (request)
                            codec::write_data(w, item.parameter, limits);
                        else {
                            w.u8(item.dar);
                            w.u8(item.data ? 1 : 0);
                            if (item.data) codec::write_data(w, *item.data, limits);
                        }
                    }
                }
                if constexpr (!request) detail::write_follow(w, v.follow_report, limits);
                detail::write_time_tag(w, v.time_tag);
            },
            message);
        return w.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
