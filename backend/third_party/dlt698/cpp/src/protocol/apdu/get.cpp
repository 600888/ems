#include "detail.hpp"

namespace dlt698::protocol::apdu {
namespace detail {
std::size_t count(Reader& r, const Limits& l, bool list) {
    const auto n = list ? codec::read_length(r, l.max_elements) : 1;
    if (!n) throw DecodeFailure({ErrorCode::invalid_length, r.position(), "GET count"});
    r.require(n, "GET items");
    return n;
}

void count(Writer& w, const Limits& l, std::size_t n, bool list) {
    if (!n || n > l.max_elements || (!list && n != 1))
        throw DecodeFailure({ErrorCode::invalid_length, w.size(), "GET count"});
    if (list) codec::write_length(w, n);
}

AttributeResult attribute(Reader& r, const Limits& l) {
    const auto oad = codec::read_oad(r);
    const auto c = r.u8("GetResult choice");
    if (c == 0) return {oad, r.u8("DAR")};
    if (c == 1) return {oad, codec::read_data(r, l)};
    detail::invalid(r.position() - 1, "GetResult choice");
    return {};
}

void attribute(Writer& w, const AttributeResult& a, const Limits& l) {
    codec::write_oad(w, a.attribute);
    w.u8(static_cast<std::uint8_t>(a.result.index()));
    if (const auto dar = std::get_if<std::uint8_t>(&a.result))
        w.u8(*dar);
    else
        codec::write_data(w, std::get<model::Data>(a.result), l);
}

RecordResult record(Reader& r, const Limits& l) {
    RecordResult v;
    v.attribute = codec::read_oad(r);
    v.columns = codec::read_rcsd(r, l);
    const auto c = r.u8("record result choice");
    if (c == 0)
        v.result = r.u8("DAR");
    else if (c == 1) {
        const auto n = codec::read_length(r, l.max_elements);
        // 行内没有数量域；表头 N 列精确决定每行读取多少个 Data。
        // 拒绝零列表头及行列乘积超限，空记录集合仍允许。
        if (n && (v.columns.empty() || n > l.max_elements / v.columns.size()))
            throw DecodeFailure({ErrorCode::resource_limit, r.position(), "record cells/header"});
        r.require(n * v.columns.size(), "record Data tags");
        std::vector<RecordRow> rows;
        for (std::size_t i = 0; i < n; ++i) {
            RecordRow row;
            for (std::size_t j = 0; j < v.columns.size(); ++j)
                row.push_back(codec::read_data(r, l));
            rows.push_back(std::move(row));
        }
        v.result = std::move(rows);
    } else
        detail::invalid(r.position() - 1, "record result choice");
    return v;
}

void record(Writer& w, const RecordResult& v, const Limits& l) {
    codec::write_oad(w, v.attribute);
    codec::write_rcsd(w, v.columns, l);
    w.u8(static_cast<std::uint8_t>(v.result.index()));
    if (const auto dar = std::get_if<std::uint8_t>(&v.result))
        w.u8(*dar);
    else {
        const auto& rows = std::get<std::vector<RecordRow>>(v.result);
        if (rows.size() > l.max_elements ||
            (!rows.empty() &&
             (v.columns.empty() || rows.size() > l.max_elements / v.columns.size())))
            throw DecodeFailure({ErrorCode::resource_limit, w.size(), "record cells/header"});
        codec::write_length(w, rows.size());
        for (const auto& row : rows) {
            if (row.size() != v.columns.size()) detail::invalid(w.size(), "record row width");
            for (const auto& d : row) codec::write_data(w, d, l);
        }
    }
}

std::optional<FollowReport> read_follow(Reader& r, const Limits& l) {
    if (!present(r, "FollowReport presence")) return {};
    const auto c = r.u8("FollowReport choice");
    if (c != 1 && c != 2) invalid(r.position() - 1, "FollowReport choice");
    const auto n = count(r, l);
    if (c == 1) {
        std::vector<AttributeResult> values;
        for (std::size_t i = 0; i < n; ++i) values.push_back(attribute(r, l));
        return FollowReport{std::move(values)};
    }
    if (c == 2) {
        std::vector<RecordResult> values;
        for (std::size_t i = 0; i < n; ++i) values.push_back(record(r, l));
        return FollowReport{std::move(values)};
    }
    invalid(r.position(), "FollowReport choice");
    return {};
}

void write_follow(Writer& w, const std::optional<FollowReport>& v, const Limits& l) {
    w.u8(v ? 1 : 0);
    if (!v) return;
    w.u8(static_cast<std::uint8_t>(v->index() + 1));
    std::visit(
        [&](const auto& values) {
            count(w, l, values.size());
            for (const auto& item : values) {
                if constexpr (std::is_same_v<std::decay_t<decltype(item)>, AttributeResult>)
                    attribute(w, item, l);
                else
                    record(w, item, l);
            }
        },
        *v);
}
}  // namespace detail

using detail::attribute;
using detail::count;
using detail::record;

Result<GetApdu> decode_get(ByteView bytes, const Limits& l) {
    if (bytes.size() > l.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "GET APDU bytes"};
    try {
        Reader r(bytes);
        const auto service = r.u8("GET service");
        if (service != 5 && service != 0x85)
            return Error{ErrorCode::unsupported_service, 0, "GET service"};
        const auto choice = r.u8("GET choice");
        const auto piid = r.u8("PIID");
        if (service == 5) detail::piid(piid, 2);
        const bool response = service == 0x85;
        GetApdu result;
        if (choice == 1 || choice == 2) {
            const auto n = count(r, l, choice == 2);
            if (!response) {
                GetRequest v{piid, choice == 2, {}, {}};
                for (std::size_t i = 0; i < n; ++i) v.attributes.push_back(codec::read_oad(r));
                result = std::move(v);
            } else {
                GetResponse v{piid, choice == 2, {}, {}};
                for (std::size_t i = 0; i < n; ++i) v.attributes.push_back(attribute(r, l));
                result = std::move(v);
            }
        } else if (choice == 3 || choice == 4) {
            const auto n = count(r, l, choice == 4);
            if (!response) {
                GetRecordRequest v{piid, choice == 4, {}, {}};
                for (std::size_t i = 0; i < n; ++i)
                    v.records.push_back(
                        {codec::read_oad(r), codec::read_rsd(r, l), codec::read_rcsd(r, l)});
                result = std::move(v);
            } else {
                GetRecordResponse v{piid, choice == 4, {}, {}};
                for (std::size_t i = 0; i < n; ++i) v.records.push_back(record(r, l));
                result = std::move(v);
            }
        } else if (choice == 5) {
            if (!response)
                result = GetNextRequest{piid, static_cast<std::uint16_t>(r.be(2, "block")), {}};
            else {
                GetNextResponse v;
                v.piid_acd = piid;
                v.last = detail::present(r, "last block");
                v.block = static_cast<std::uint16_t>(r.be(2, "block"));
                const auto c = r.u8("Next result choice");
                if (c == 0) {
                    v.result = r.u8("DAR");
                    if (!v.last) detail::invalid(r.position(), "nonfinal DAR block");
                } else if (c == 1) {
                    std::vector<AttributeResult> values;
                    const auto n = count(r, l);
                    for (std::size_t i = 0; i < n; ++i) values.push_back(attribute(r, l));
                    v.result = std::move(values);
                } else if (c == 2) {
                    std::vector<RecordResult> values;
                    const auto n = count(r, l);
                    for (std::size_t i = 0; i < n; ++i) values.push_back(record(r, l));
                    v.result = std::move(values);
                } else
                    detail::invalid(r.position() - 1, "Next result choice");
                result = std::move(v);
            }
        } else if (choice == 6) {
            const auto oad = codec::read_oad(r);
            if (!response)
                result = GetMd5Request{piid, oad, {}};
            else {
                GetMd5Response v;
                v.piid_acd = piid;
                v.attribute = oad;
                const auto c = r.u8("MD5 result choice");
                if (c == 0)
                    v.result = r.u8("DAR");
                else if (c == 1) {
                    if (codec::read_length(r, 16) != 16)
                        detail::invalid(r.position(), "MD5 length");
                    std::array<std::uint8_t, 16> digest{};
                    for (auto& b : digest) b = r.u8("MD5");
                    v.result = digest;
                } else
                    detail::invalid(r.position() - 1, "MD5 result choice");
                result = std::move(v);
            }
        } else
            return Error{ErrorCode::unsupported_service, 1, "GET variant"};
        if (response)
            std::visit(
                [&](auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, GetResponse> ||
                                  std::is_same_v<T, GetRecordResponse> ||
                                  std::is_same_v<T, GetNextResponse> ||
                                  std::is_same_v<T, GetMd5Response>)
                        v.follow_report = detail::read_follow(r, l);
                },
                result);
        std::visit([&](auto& v) { v.time_tag = detail::read_time_tag(r); }, result);
        r.finish();
        return result;
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_get(const GetApdu& message, const Limits& l) {
    try {
        Writer w(l.max_data_bytes);
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                constexpr bool response =
                    std::is_same_v<T, GetResponse> || std::is_same_v<T, GetRecordResponse> ||
                    std::is_same_v<T, GetNextResponse> || std::is_same_v<T, GetMd5Response>;
                w.u8(response ? 0x85 : 5);
                if constexpr (std::is_same_v<T, GetRequest> || std::is_same_v<T, GetResponse>)
                    w.u8(v.list ? 2 : 1);
                else if constexpr (std::is_same_v<T, GetRecordRequest> ||
                                   std::is_same_v<T, GetRecordResponse>)
                    w.u8(v.list ? 4 : 3);
                else if constexpr (std::is_same_v<T, GetMd5Request> ||
                                   std::is_same_v<T, GetMd5Response>)
                    w.u8(6);
                else
                    w.u8(5);
                if constexpr (response)
                    w.u8(v.piid_acd);
                else {
                    detail::piid(v.piid, 2);
                    w.u8(v.piid);
                }
                if constexpr (std::is_same_v<T, GetRequest>) {
                    count(w, l, v.attributes.size(), v.list);
                    for (const auto& a : v.attributes) codec::write_oad(w, a);
                } else if constexpr (std::is_same_v<T, GetResponse>) {
                    count(w, l, v.attributes.size(), v.list);
                    for (const auto& a : v.attributes) attribute(w, a, l);
                } else if constexpr (std::is_same_v<T, GetRecordRequest>) {
                    count(w, l, v.records.size(), v.list);
                    for (const auto& a : v.records) {
                        codec::write_oad(w, a.attribute);
                        codec::write_rsd(w, a.rows, l);
                        codec::write_rcsd(w, a.columns, l);
                    }
                } else if constexpr (std::is_same_v<T, GetRecordResponse>) {
                    count(w, l, v.records.size(), v.list);
                    for (const auto& a : v.records) record(w, a, l);
                } else if constexpr (std::is_same_v<T, GetNextRequest>)
                    w.be(v.block, 2);
                else if constexpr (std::is_same_v<T, GetMd5Request> ||
                                   std::is_same_v<T, GetMd5Response>) {
                    codec::write_oad(w, v.attribute);
                    if constexpr (response) {
                        w.u8(static_cast<std::uint8_t>(v.result.index()));
                        if (const auto dar = std::get_if<std::uint8_t>(&v.result))
                            w.u8(*dar);
                        else {
                            const auto& digest = std::get<std::array<std::uint8_t, 16>>(v.result);
                            codec::write_length(w, digest.size());
                            w.bytes({digest.data(), digest.size()});
                        }
                    }
                } else {
                    w.u8(v.last ? 1 : 0);
                    w.be(v.block, 2);
                    w.u8(static_cast<std::uint8_t>(v.result.index()));
                    if (const auto dar = std::get_if<std::uint8_t>(&v.result)) {
                        if (!v.last) detail::invalid(w.size(), "nonfinal DAR block");
                        w.u8(*dar);
                    } else if (const auto attrs =
                                   std::get_if<std::vector<AttributeResult>>(&v.result)) {
                        count(w, l, attrs->size());
                        for (const auto& a : *attrs) attribute(w, a, l);
                    } else {
                        const auto& records = std::get<std::vector<RecordResult>>(v.result);
                        count(w, l, records.size());
                        for (const auto& a : records) record(w, a, l);
                    }
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
