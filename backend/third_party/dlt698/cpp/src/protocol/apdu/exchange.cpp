#include <dlt698/protocol/apdu/apdu.hpp>

namespace dlt698::protocol::apdu {
unsigned advanced_capability(const Apdu& message) {
    if (std::holds_alternative<GetMd5Request>(message)) return 1;
    if (std::holds_alternative<SetThenGetRequest>(message)) return 9;
    if (std::holds_alternative<ActionThenGetRequest>(message)) return 14;
    if (std::holds_alternative<ReportNotification>(message)) return 17;
    if (const auto v = std::get_if<ProxyRequest>(&message)) {
        constexpr unsigned bits[]{4, 5, 10, 11, 15, 16, 64};
        return bits[v->payload.index()];
    }
    return 65;
}

namespace {
template <class Q, class R>
bool descriptors(const Q& q, const R& r) {
    if constexpr (std::is_same_v<Q, model::Oad>)
        return q == r.attribute;
    else if constexpr (std::is_same_v<Q, SetAttribute>)
        return q.attribute == r.attribute;
    else if constexpr (std::is_same_v<Q, ActionMethod>)
        return q.method == r.method;
    else if constexpr (std::is_same_v<Q, SetThenGet>)
        return q.set.attribute == r.set.attribute && q.read == r.read.attribute;
    else
        return q.action.method == r.action.method && q.read == r.read.attribute;
}

template <class Q, class R>
bool list(const std::vector<Q>& q, const std::vector<R>& r) {
    if (q.size() != r.size()) return false;
    for (std::size_t i = 0; i < q.size(); ++i)
        if (!descriptors(q[i], r[i])) return false;
    return true;
}

template <std::size_t I>
bool proxy_match(const ProxyRequestPayload& q, const ProxyResponsePayload& r) {
    const auto& a = std::get<I>(q);
    const auto& b = std::get<I>(r);
    if constexpr (I == 1)
        return a.server == b.server && a.record.attribute == b.record.attribute &&
               (a.record.columns.empty() || a.record.columns == b.record.columns);
    else if constexpr (I == 6)
        return a.port == b.port;
    else {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!(a[i].server == b[i].server) || !list(a[i].items, b[i].items)) return false;
        return true;
    }
}
}  // namespace

bool advanced_matches(const Apdu& request, const Apdu& response) {
    if (const auto q = std::get_if<GetMd5Request>(&request)) {
        const auto r = std::get_if<GetMd5Response>(&response);
        return r && q->attribute == r->attribute;
    }
    if (const auto q = std::get_if<SetThenGetRequest>(&request)) {
        const auto r = std::get_if<SetThenGetResponse>(&response);
        return r && list(q->items, r->items);
    }
    if (const auto q = std::get_if<ActionThenGetRequest>(&request)) {
        const auto r = std::get_if<ActionThenGetResponse>(&response);
        return r && list(q->items, r->items);
    }
    if (const auto q = std::get_if<ReportNotification>(&request)) {
        const auto r = std::get_if<ReportResponse>(&response);
        if (!r || r->choice != q->payload.index() + 1) return false;
        return std::visit(
            [&](const auto& v) {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>, TransData>)
                    return r->attributes.empty();
                else {
                    if (v.size() != r->attributes.size()) return false;
                    for (std::size_t i = 0; i < v.size(); ++i)
                        if (!(v[i].attribute == r->attributes[i])) return false;
                    return true;
                }
            },
            q->payload);
    }
    if (const auto q = std::get_if<ProxyRequest>(&request)) {
        const auto r = std::get_if<ProxyResponse>(&response);
        if (!r || q->payload.index() != r->payload.index()) return false;
        switch (q->payload.index()) {
            case 0:
                return proxy_match<0>(q->payload, r->payload);
            case 1:
                return proxy_match<1>(q->payload, r->payload);
            case 2:
                return proxy_match<2>(q->payload, r->payload);
            case 3:
                return proxy_match<3>(q->payload, r->payload);
            case 4:
                return proxy_match<4>(q->payload, r->payload);
            case 5:
                return proxy_match<5>(q->payload, r->payload);
            default:
                return proxy_match<6>(q->payload, r->payload);
        }
    }
    return false;
}
}  // namespace dlt698::protocol::apdu
