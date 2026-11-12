#include <dlt698/protocol/apdu/connection.hpp>

#include "detail.hpp"

namespace dlt698::protocol::apdu {
namespace {
using detail::invalid;

template <std::size_t N>
void read_fixed(Reader& r, std::array<std::uint8_t, N>& a) {
    for (auto& b : a) b = r.u8("fixed field");
}

template <std::size_t N>
void write_fixed(Writer& w, const std::array<std::uint8_t, N>& a) {
    w.bytes({a.data(), a.size()});
}

AssociationParameters read_parameters(Reader& r) {
    AssociationParameters p;
    p.version = static_cast<std::uint16_t>(r.be(2, "protocol version"));
    // 固定长度位图在线上没有位数前缀，位序按附录 C 保持不变。
    read_fixed(r, p.protocol);
    read_fixed(r, p.function);
    p.send_frame_bytes = static_cast<std::uint16_t>(r.be(2, "send frame limit"));
    p.receive_frame_bytes = static_cast<std::uint16_t>(r.be(2, "receive frame limit"));
    p.receive_window = r.u8("receive window");
    p.apdu_bytes = static_cast<std::uint16_t>(r.be(2, "APDU limit"));
    p.timeout_seconds = static_cast<std::uint32_t>(r.be(4, "association timeout"));
    return p;
}

void write_parameters(Writer& w, const AssociationParameters& p) {
    w.be(p.version, 2);
    write_fixed(w, p.protocol);
    write_fixed(w, p.function);
    w.be(p.send_frame_bytes, 2);
    w.be(p.receive_frame_bytes, 2);
    w.u8(p.receive_window);
    w.be(p.apdu_bytes, 2);
    w.be(p.timeout_seconds, 4);
}

Bytes read_string(Reader& r, const Limits& limits) {
    const auto n = codec::read_length(r, limits.max_data_bytes);
    return r.bytes(n, "authentication bytes");
}

void write_string(Writer& w, ByteView value) {
    codec::write_length(w, value.size());
    w.bytes(value);
}

void check_password(const Bytes& value, std::size_t offset) {
    for (auto b : value)
        if (b < 0x20 || b > 0x7e) invalid(offset, "password visible-string");
}

ConnectMechanism read_mechanism(Reader& r, const Limits& limits) {
    const auto offset = r.position();
    switch (r.u8("authentication choice")) {
        case 0:
            return NullSecurity{};
        case 1: {
            auto bytes = read_string(r, limits);
            check_password(bytes, offset);
            return PasswordSecurity{std::string(bytes.begin(), bytes.end())};
        }
        case 2: {
            auto a = read_string(r, limits);
            auto b = read_string(r, limits);
            return SymmetrySecurity{std::move(a), std::move(b)};
        }
        case 3: {
            auto a = read_string(r, limits);
            auto b = read_string(r, limits);
            return SignatureSecurity{std::move(a), std::move(b)};
        }
        default:
            invalid(offset, "authentication choice");
    }
    return NullSecurity{};
}

void write_mechanism(Writer& w, const ConnectMechanism& mechanism) {
    w.u8(static_cast<std::uint8_t>(mechanism.index()));
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, PasswordSecurity>) {
                const Bytes bytes(v.password.begin(), v.password.end());
                check_password(bytes, w.size());
                write_string(w, bytes);
            } else if constexpr (!std::is_same_v<T, NullSecurity>) {
                write_string(w, v.ciphertext);
                write_string(w, v.signature);
            }
        },
        mechanism);
}

void check_link_result(std::uint8_t value, std::size_t offset) {
    if ((value & 0x78) || (value & 7) > 3) invalid(offset, "LINK result");
}

void check_connect_result(std::uint8_t value, std::size_t offset) {
    if (value > 5 && value != 255) invalid(offset, "CONNECT result");
}

void check_error_type(std::uint8_t value, std::size_t offset) {
    if (value != 1 && value != 2 && value != 255) invalid(offset, "ERROR type");
}
}  // namespace

Result<ConnectionApdu> decode_connection(ByteView bytes, const Limits& limits) {
    if (bytes.size() > limits.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "APDU input limit"};
    try {
        Reader r(bytes);
        const auto service = r.u8("connection service");
        const auto id = r.u8("PIID");
        ConnectionApdu result;
        if (service == 1) {
            LinkRequest v;
            v.piid_acd = id;
            const auto type = r.u8("LINK type");
            if (type > 2) invalid(2, "LINK type");
            v.type = static_cast<LinkRequestType>(type);
            v.heartbeat_seconds = static_cast<std::uint16_t>(r.be(2, "heartbeat period"));
            v.requested_at = detail::calendar<model::DateTime>(r);
            result = v;
        } else if (service == 0x81) {
            detail::piid(id, 1);
            LinkResponse v;
            v.piid = id;
            v.result = r.u8("LINK result");
            check_link_result(v.result, 2);
            v.requested_at = detail::calendar<model::DateTime>(r);
            v.received_at = detail::calendar<model::DateTime>(r);
            v.responded_at = detail::calendar<model::DateTime>(r);
            result = v;
        } else if (service == 2) {
            detail::piid(id, 1);
            ConnectRequest v;
            v.piid = id;
            v.parameters = read_parameters(r);
            v.mechanism = read_mechanism(r, limits);
            v.time_tag = detail::read_time_tag(r);
            result = std::move(v);
        } else if (service == 0x82) {
            ConnectResponse v;
            v.piid_acd = id;
            read_fixed(r, v.factory.manufacturer);
            read_fixed(r, v.factory.software_version);
            read_fixed(r, v.factory.software_date);
            read_fixed(r, v.factory.hardware_version);
            read_fixed(r, v.factory.hardware_date);
            read_fixed(r, v.factory.extension);
            v.parameters = read_parameters(r);
            v.result = r.u8("CONNECT result");
            check_connect_result(v.result, r.position() - 1);
            if (detail::present(r, "SecurityData presence")) {
                auto random = read_string(r, limits);
                auto signature = read_string(r, limits);
                v.security = SecurityData{std::move(random), std::move(signature)};
            }
            v.follow_report = detail::read_follow(r, limits);
            v.time_tag = detail::read_time_tag(r);
            result = std::move(v);
        } else if (service == 3) {
            detail::piid(id, 1);
            result = ReleaseRequest{id, detail::read_time_tag(r)};
        } else if (service == 0x83) {
            const auto code = r.u8("RELEASE result");
            if (code) invalid(2, "RELEASE result");
            auto follow = detail::read_follow(r, limits);
            result = ReleaseResponse{id, code, detail::read_time_tag(r), std::move(follow)};
        } else if (service == 0x84) {
            ReleaseNotification v;
            v.piid_acd = id;
            v.established_at = detail::calendar<model::DateTimeS>(r);
            v.current_time = detail::calendar<model::DateTimeS>(r);
            v.follow_report = detail::read_follow(r, limits);
            v.time_tag = detail::read_time_tag(r);
            result = v;
        } else if (service == 0x6e || service == 0xee) {
            detail::piid(id, 1);
            const auto type = r.u8("ERROR type");
            check_error_type(type, 2);
            auto follow =
                service == 0xee ? detail::read_follow(r, limits) : std::optional<FollowReport>{};
            result = ErrorResponse{service == 0xee, id, type, detail::read_time_tag(r),
                                   std::move(follow)};
        } else
            return Error{ErrorCode::unsupported_service, 0, "connection service"};
        // LINK 不属于 Client/Server-APDU，不存在 FollowReport 或 TimeTag 尾部。
        r.finish();
        return result;
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_connection(const ConnectionApdu& message, const Limits& limits) {
    try {
        Writer w(limits.max_data_bytes);
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, LinkRequest>) {
                    if (static_cast<unsigned>(v.type) > 2) invalid(2, "LINK type");
                    w.u8(1);
                    w.u8(v.piid_acd);
                    w.u8(static_cast<std::uint8_t>(v.type));
                    w.be(v.heartbeat_seconds, 2);
                    write_fixed(w, v.requested_at.value);
                } else if constexpr (std::is_same_v<T, LinkResponse>) {
                    detail::piid(v.piid, 1);
                    check_link_result(v.result, 2);
                    w.u8(0x81);
                    w.u8(v.piid);
                    w.u8(v.result);
                    write_fixed(w, v.requested_at.value);
                    write_fixed(w, v.received_at.value);
                    write_fixed(w, v.responded_at.value);
                } else {
                    bool server = true;
                    if constexpr (std::is_same_v<T, ConnectRequest>) {
                        server = false;
                        detail::piid(v.piid, 1);
                        w.u8(2);
                        w.u8(v.piid);
                        write_parameters(w, v.parameters);
                        write_mechanism(w, v.mechanism);
                    } else if constexpr (std::is_same_v<T, ConnectResponse>) {
                        check_connect_result(v.result, 71);
                        w.u8(0x82);
                        w.u8(v.piid_acd);
                        write_fixed(w, v.factory.manufacturer);
                        write_fixed(w, v.factory.software_version);
                        write_fixed(w, v.factory.software_date);
                        write_fixed(w, v.factory.hardware_version);
                        write_fixed(w, v.factory.hardware_date);
                        write_fixed(w, v.factory.extension);
                        write_parameters(w, v.parameters);
                        w.u8(v.result);
                        w.u8(v.security ? 1 : 0);
                        if (v.security) {
                            write_string(w, v.security->random);
                            write_string(w, v.security->signature);
                        }
                    } else if constexpr (std::is_same_v<T, ReleaseRequest>) {
                        server = false;
                        detail::piid(v.piid, 1);
                        w.u8(3);
                        w.u8(v.piid);
                    } else if constexpr (std::is_same_v<T, ReleaseResponse>) {
                        if (v.result) invalid(2, "RELEASE result");
                        w.u8(0x83);
                        w.u8(v.piid_acd);
                        w.u8(v.result);
                    } else if constexpr (std::is_same_v<T, ReleaseNotification>) {
                        w.u8(0x84);
                        w.u8(v.piid_acd);
                        write_fixed(w, v.established_at.value);
                        write_fixed(w, v.current_time.value);
                    } else if constexpr (std::is_same_v<T, ErrorResponse>) {
                        server = v.server;
                        detail::piid(v.piid, 1);
                        check_error_type(v.type, 2);
                        w.u8(server ? 0xee : 0x6e);
                        w.u8(v.piid);
                        w.u8(v.type);
                    }
                    if constexpr (std::is_same_v<T, ConnectResponse> ||
                                  std::is_same_v<T, ReleaseResponse> ||
                                  std::is_same_v<T, ReleaseNotification> ||
                                  std::is_same_v<T, ErrorResponse>) {
                        if (server)
                            detail::write_follow(w, v.follow_report, limits);
                        else if (v.follow_report)
                            invalid(w.size(), "client FollowReport");
                    }
                    detail::write_time_tag(w, v.time_tag);
                }
            },
            message);
        return w.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
