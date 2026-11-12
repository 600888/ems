#include <dlt698/protocol/apdu/advanced.hpp>

#include "../../codec/security_detail.hpp"
#include "detail.hpp"

namespace dlt698::protocol::apdu {
namespace {
namespace fields = codec::fields;

// PROXY 指定单个被代理设备，组地址/通配地址不能承担逐目标结果匹配。
model::Tsa single_tsa(Reader& r, const Limits& l) {
    auto v = fields::tsa(r, l);
    if (v.value[0] & 0xc0) detail::invalid(r.position(), "proxy single TSA");
    return v;
}

void single_tsa(Writer& w, const model::Tsa& v) {
    if (!v.value.empty() && (v.value[0] & 0xc0)) detail::invalid(w.size(), "proxy single TSA");
    fields::tsa(w, v);
}

model::Omd method(Reader& r) {
    return {static_cast<std::uint16_t>(r.be(2, "OI")), r.u8("method"), r.u8("mode")};
}

void method(Writer& w, const model::Omd& v) {
    w.be(v.oi, 2);
    w.u8(v.method);
    w.u8(v.mode);
}

template <class T>
T item(Reader& r, const Limits& l) {
    if constexpr (std::is_same_v<T, model::Oad>)
        return codec::read_oad(r);
    else if constexpr (std::is_same_v<T, AttributeResult>)
        return detail::attribute(r, l);
    else if constexpr (std::is_same_v<T, RecordResult>)
        return detail::record(r, l);
    else if constexpr (std::is_same_v<T, SetAttribute>) {
        const auto a = codec::read_oad(r);
        return {a, codec::read_data(r, l)};
    } else if constexpr (std::is_same_v<T, SetResult>) {
        const auto a = codec::read_oad(r);
        return {a, r.u8("DAR")};
    } else if constexpr (std::is_same_v<T, ActionMethod>) {
        const auto m = method(r);
        return {m, codec::read_data(r, l)};
    } else if constexpr (std::is_same_v<T, ActionResult>) {
        const auto m = method(r);
        const auto dar = r.u8("DAR");
        std::optional<model::Data> data;
        if (detail::present(r, "action Data presence")) data = codec::read_data(r, l);
        return {m, dar, std::move(data)};
    } else if constexpr (std::is_same_v<T, SetThenGet>) {
        auto set = item<SetAttribute>(r, l);
        const auto a = codec::read_oad(r);
        return {std::move(set), a, r.u8("read delay")};
    } else if constexpr (std::is_same_v<T, ActionThenGet>) {
        auto action = item<ActionMethod>(r, l);
        const auto a = codec::read_oad(r);
        return {std::move(action), a, r.u8("read delay")};
    } else if constexpr (std::is_same_v<T, SetThenGetResult>) {
        auto set = item<SetResult>(r, l);
        return {std::move(set), detail::attribute(r, l)};
    } else {
        auto action = item<ActionResult>(r, l);
        return {std::move(action), detail::attribute(r, l)};
    }
}

template <class T>
void item(Writer& w, const T& v, const Limits& l) {
    if constexpr (std::is_same_v<T, model::Oad>)
        codec::write_oad(w, v);
    else if constexpr (std::is_same_v<T, AttributeResult>)
        detail::attribute(w, v, l);
    else if constexpr (std::is_same_v<T, RecordResult>)
        detail::record(w, v, l);
    else if constexpr (std::is_same_v<T, SetAttribute>) {
        codec::write_oad(w, v.attribute);
        codec::write_data(w, v.value, l);
    } else if constexpr (std::is_same_v<T, SetResult>) {
        codec::write_oad(w, v.attribute);
        w.u8(v.dar);
    } else if constexpr (std::is_same_v<T, ActionMethod>) {
        method(w, v.method);
        codec::write_data(w, v.parameter, l);
    } else if constexpr (std::is_same_v<T, ActionResult>) {
        method(w, v.method);
        w.u8(v.dar);
        w.u8(v.data ? 1 : 0);
        if (v.data) codec::write_data(w, *v.data, l);
    } else if constexpr (std::is_same_v<T, SetThenGet>) {
        item(w, v.set, l);
        codec::write_oad(w, v.read);
        w.u8(v.delay_seconds);
    } else if constexpr (std::is_same_v<T, ActionThenGet>) {
        item(w, v.action, l);
        codec::write_oad(w, v.read);
        w.u8(v.delay_seconds);
    } else if constexpr (std::is_same_v<T, SetThenGetResult>) {
        item(w, v.set, l);
        detail::attribute(w, v.read, l);
    } else {
        item(w, v.action, l);
        detail::attribute(w, v.read, l);
    }
}

template <class T>
std::vector<T> items(Reader& r, const Limits& l) {
    const auto n = detail::count(r, l);
    std::vector<T> v;
    for (std::size_t i = 0; i < n; ++i) v.push_back(item<T>(r, l));
    return v;
}

template <class T>
void items(Writer& w, const std::vector<T>& v, const Limits& l) {
    detail::count(w, l, v.size());
    for (const auto& a : v) item(w, a, l);
}

template <class T>
std::vector<ProxyTarget<T>> targets(Reader& r, const Limits& l, bool request) {
    const auto n = detail::count(r, l);
    std::vector<ProxyTarget<T>> v;
    std::size_t total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        auto tsa = single_tsa(r, l);
        const auto timeout =
            request ? static_cast<std::uint16_t>(r.be(2, "target timeout")) : std::uint16_t{0};
        auto list = items<T>(r, l);
        // 外层与各目标的内层数量共享总项预算，防止嵌套列表放大资源。
        if (list.size() > l.max_elements - total)
            throw DecodeFailure({ErrorCode::resource_limit, r.position(), "proxy total items"});
        total += list.size();
        v.push_back({std::move(tsa), timeout, std::move(list)});
    }
    return v;
}

template <class T>
void targets(Writer& w, const std::vector<ProxyTarget<T>>& v, const Limits& l, bool request) {
    detail::count(w, l, v.size());
    std::size_t total = 0;
    for (const auto& target : v) {
        if (target.items.size() > l.max_elements - total)
            throw DecodeFailure({ErrorCode::resource_limit, w.size(), "proxy total items"});
        total += target.items.size();
        single_tsa(w, target.server);
        if (request) w.be(target.timeout_seconds, 2);
        items(w, target.items, l);
    }
}

ProxyRequestPayload proxy_request(Reader& r, const Limits& l, std::uint8_t c) {
    switch (c) {
        case 1:
            return targets<model::Oad>(r, l, true);
        case 2: {
            auto tsa = single_tsa(r, l);
            auto a = codec::read_oad(r);
            auto rsd = codec::read_rsd(r, l);
            auto rcsd = codec::read_rcsd(r, l);
            return ProxyRecordRequest{std::move(tsa), {a, std::move(rsd), std::move(rcsd)}};
        }
        case 3:
            return targets<SetAttribute>(r, l, true);
        case 4:
            return targets<SetThenGet>(r, l, true);
        case 5:
            return targets<ActionMethod>(r, l, true);
        case 6:
            return targets<ActionThenGet>(r, l, true);
        default: {
            auto a = codec::read_oad(r);
            auto com = fields::comdcb(r);
            const auto timeout = static_cast<std::uint16_t>(r.be(2, "response timeout"));
            const auto byte_timeout = static_cast<std::uint16_t>(r.be(2, "byte timeout"));
            return ProxyTransRequest{a, com, timeout, byte_timeout, fields::octets(r, l)};
        }
    }
}

ProxyResponsePayload proxy_response(Reader& r, const Limits& l, std::uint8_t c) {
    switch (c) {
        case 1:
            return targets<AttributeResult>(r, l, false);
        case 2: {
            auto tsa = single_tsa(r, l);
            return ProxyRecordResponse{std::move(tsa), detail::record(r, l)};
        }
        case 3:
            return targets<SetResult>(r, l, false);
        case 4:
            return targets<SetThenGetResult>(r, l, false);
        case 5:
            return targets<ActionResult>(r, l, false);
        case 6:
            return targets<ActionThenGetResult>(r, l, false);
        default: {
            ProxyTransResponse v;
            v.port = codec::read_oad(r);
            const auto choice = r.u8("TransResult choice");
            if (choice == 0)
                v.result = r.u8("DAR");
            else if (choice == 1)
                v.result = fields::octets(r, l);
            else
                detail::invalid(r.position() - 1, "TransResult choice");
            return v;
        }
    }
}

template <class P>
void proxy(Writer& w, const P& payload, const Limits& l) {
    constexpr bool request = std::is_same_v<P, ProxyRequestPayload>;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ProxyRecordRequest>) {
                single_tsa(w, v.server);
                codec::write_oad(w, v.record.attribute);
                codec::write_rsd(w, v.record.rows, l);
                codec::write_rcsd(w, v.record.columns, l);
            } else if constexpr (std::is_same_v<T, ProxyRecordResponse>) {
                single_tsa(w, v.server);
                detail::record(w, v.record, l);
            } else if constexpr (std::is_same_v<T, ProxyTransRequest>) {
                codec::write_oad(w, v.port);
                fields::comdcb(w, v.communication);
                w.be(v.response_timeout_seconds, 2);
                w.be(v.byte_timeout_milliseconds, 2);
                fields::octets(w, v.command);
            } else if constexpr (std::is_same_v<T, ProxyTransResponse>) {
                codec::write_oad(w, v.port);
                w.u8(static_cast<std::uint8_t>(v.result.index()));
                if (const auto dar = std::get_if<std::uint8_t>(&v.result))
                    w.u8(*dar);
                else
                    fields::octets(w, std::get<Bytes>(v.result));
            } else
                targets(w, v, l, request);
        },
        payload);
}
}  // namespace

Result<AdvancedApdu> decode_advanced(ByteView bytes, const Limits& l) {
    if (bytes.size() > l.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "advanced APDU bytes"};
    try {
        Reader r(bytes);
        const auto service = r.u8("advanced service");
        const auto c = r.u8("choice");
        if (!((c == 3 && (service == 6 || service == 7 || service == 0x86 || service == 0x87)) ||
              (c >= 1 && c <= 3 && (service == 8 || service == 0x88)) ||
              (c >= 1 && c <= 7 && (service == 9 || service == 0x89))))
            return Error{ErrorCode::unsupported_service, 0, "advanced service/choice"};
        const auto id = r.u8("PIID");
        if (service < 0x80) detail::piid(id, 2);
        AdvancedApdu result;
        if (c == 3 && service == 6)
            result = SetThenGetRequest{id, items<SetThenGet>(r, l), {}};
        else if (c == 3 && service == 0x86)
            result = SetThenGetResponse{id, items<SetThenGetResult>(r, l), {}, {}};
        else if (c == 3 && service == 7)
            result = ActionThenGetRequest{id, items<ActionThenGet>(r, l), {}};
        else if (c == 3 && service == 0x87)
            result = ActionThenGetResponse{id, items<ActionThenGetResult>(r, l), {}, {}};
        else if (service == 0x88 && c >= 1 && c <= 3) {
            ReportNotification v;
            v.piid_acd = id;
            if (c == 1)
                v.payload = items<AttributeResult>(r, l);
            else if (c == 2)
                v.payload = items<RecordResult>(r, l);
            else {
                TransData t;
                t.port = codec::read_oad(r);
                const auto n = detail::count(r, l);
                for (std::size_t i = 0; i < n; ++i) t.data.push_back(fields::octets(r, l));
                v.payload = std::move(t);
            }
            result = std::move(v);
        } else if (service == 8 && c >= 1 && c <= 3) {
            ReportResponse v{id, c, {}, {}};
            if (c != 3) v.attributes = items<model::Oad>(r, l);
            result = std::move(v);
        } else if (service == 9 && c >= 1 && c <= 7) {
            ProxyRequest v;
            v.piid = id;
            if (c != 7) {
                v.timeout_seconds = static_cast<std::uint16_t>(r.be(2, "proxy timeout"));
                if (!v.timeout_seconds) detail::invalid(r.position() - 2, "zero proxy timeout");
            }
            v.payload = proxy_request(r, l, c);
            result = std::move(v);
        } else if (service == 0x89 && c >= 1 && c <= 7) {
            ProxyResponse v;
            v.piid_acd = id;
            v.payload = proxy_response(r, l, c);
            result = std::move(v);
        } else
            return Error{ErrorCode::unsupported_service, 0, "advanced service/choice"};
        std::visit(
            [&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, SetThenGetResponse> ||
                              std::is_same_v<T, ActionThenGetResponse> ||
                              std::is_same_v<T, ReportNotification> ||
                              std::is_same_v<T, ProxyResponse>)
                    v.follow_report = detail::read_follow(r, l);
                v.time_tag = detail::read_time_tag(r);
            },
            result);
        r.finish();
        return result;
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_advanced(const AdvancedApdu& message, const Limits& l) {
    try {
        Writer w(l.max_data_bytes);
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                constexpr bool response = std::is_same_v<T, SetThenGetResponse> ||
                                          std::is_same_v<T, ActionThenGetResponse> ||
                                          std::is_same_v<T, ReportNotification> ||
                                          std::is_same_v<T, ProxyResponse>;
                if constexpr (std::is_same_v<T, ReportNotification>) {
                    w.u8(0x88);
                    w.u8(static_cast<std::uint8_t>(v.payload.index() + 1));
                    w.u8(v.piid_acd);
                    std::visit(
                        [&](const auto& payload) {
                            if constexpr (std::is_same_v<std::decay_t<decltype(payload)>,
                                                         TransData>) {
                                codec::write_oad(w, payload.port);
                                detail::count(w, l, payload.data.size());
                                for (const auto& b : payload.data) fields::octets(w, b);
                            } else
                                items(w, payload, l);
                        },
                        v.payload);
                } else if constexpr (std::is_same_v<T, ReportResponse>) {
                    if (v.choice < 1 || v.choice > 3 || (v.choice == 3 && !v.attributes.empty()))
                        detail::invalid(1, "report response choice/items");
                    w.u8(8);
                    w.u8(v.choice);
                    detail::piid(v.piid, 2);
                    w.u8(v.piid);
                    if (v.choice != 3) items(w, v.attributes, l);
                } else if constexpr (std::is_same_v<T, ProxyRequest> ||
                                     std::is_same_v<T, ProxyResponse>) {
                    w.u8(response ? 0x89 : 9);
                    w.u8(static_cast<std::uint8_t>(v.payload.index() + 1));
                    if constexpr (response)
                        w.u8(v.piid_acd);
                    else {
                        detail::piid(v.piid, 2);
                        w.u8(v.piid);
                        if (v.payload.index() != 6) {
                            if (!v.timeout_seconds) detail::invalid(w.size(), "zero proxy timeout");
                            w.be(v.timeout_seconds, 2);
                        }
                    }
                    proxy(w, v.payload, l);
                } else {
                    constexpr bool set = std::is_same_v<T, SetThenGetRequest> ||
                                         std::is_same_v<T, SetThenGetResponse>;
                    w.u8(static_cast<std::uint8_t>((set ? 6 : 7) + (response ? 0x80 : 0)));
                    w.u8(3);
                    if constexpr (response)
                        w.u8(v.piid_acd);
                    else {
                        detail::piid(v.piid, 2);
                        w.u8(v.piid);
                    }
                    items(w, v.items, l);
                }
                if constexpr (response) detail::write_follow(w, v.follow_report, l);
                detail::write_time_tag(w, v.time_tag);
            },
            message);
        return w.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
