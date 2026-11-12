#include <dlt698/protocol/apdu/security.hpp>

#include "../../codec/security_detail.hpp"
#include "detail.hpp"

namespace dlt698::protocol::apdu {
namespace f = codec::fields;

Result<SecurityApdu> decode_security(ByteView bytes, const Limits& l) {
    if (bytes.size() > l.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "SECURITY bytes"};
    try {
        Reader r(bytes);
        const auto service = r.u8("SECURITY service");
        const auto c = r.u8("SECURITY application choice");
        // 安全封装没有 PIID、FollowReport 或 TimeTag，内层 APDU 作为不透明字节保存。
        // 此处只检查线格式；MAC/签名与重放校验必须由安全后端在业务解码前完成。
        SecurityApdu result;
        if (service == 0x10) {
            if (c > 1) detail::invalid(1, "SECURITY request application choice");
            SecurityRequest v;
            v.encrypted = c == 1;
            v.application = f::octets(r, l);
            const auto verification = r.u8("SECURITY request verification choice");
            if (verification == 0)
                v.verification = f::sid_mac(r, l);
            else if (verification == 1)
                v.verification = model::Rn{f::octets(r, l)};
            else if (verification == 2) {
                auto random = f::octets(r, l);
                auto mac = f::octets(r, l);
                v.verification = RnMac{model::Rn{std::move(random)}, model::Mac{std::move(mac)}};
            } else if (verification == 3)
                v.verification = f::sid(r, l);
            else
                detail::invalid(r.position() - 1, "SECURITY request verification choice");
            result = std::move(v);
        } else if (service == 0x90) {
            SecurityResponse v;
            // 响应第三分支仅含验证 DAR，不能将它解释成零长度明文。
            if (c == 2)
                v.application = r.u8("security DAR");
            else if (c < 2) {
                v.encrypted = c == 1;
                v.application = f::octets(r, l);
            } else
                detail::invalid(1, "SECURITY response application choice");
            if (detail::present(r, "SECURITY verification presence")) {
                const auto verification = r.u8("SECURITY response verification choice");
                if (verification == 0)
                    v.verification = model::Mac{f::octets(r, l)};
                else if (verification == 1)
                    v.verification = f::sid_mac(r, l);
                else
                    detail::invalid(r.position() - 1, "SECURITY response verification choice");
            }
            result = std::move(v);
        } else
            return Error{ErrorCode::unsupported_service, 0, "SECURITY service"};
        r.finish();
        return result;
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}

Result<Bytes> encode_security(const SecurityApdu& message, const Limits& l) {
    try {
        // 密文和验证字段共同消耗输出上限，不能仅检查内层明文尺寸。
        Writer w(l.max_data_bytes);
        std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, SecurityRequest>) {
                    w.u8(0x10);
                    w.u8(v.encrypted ? 1 : 0);
                    f::octets(w, v.application);
                    w.u8(static_cast<std::uint8_t>(v.verification.index()));
                    std::visit(
                        [&](const auto& verify) {
                            using V = std::decay_t<decltype(verify)>;
                            if constexpr (std::is_same_v<V, model::SidMac>)
                                f::sid_mac(w, verify);
                            else if constexpr (std::is_same_v<V, model::Sid>)
                                f::sid(w, verify);
                            else if constexpr (std::is_same_v<V, model::Rn>)
                                f::octets(w, verify.value);
                            else {
                                f::octets(w, verify.random.value);
                                f::octets(w, verify.mac.value);
                            }
                        },
                        v.verification);
                } else {
                    w.u8(0x90);
                    if (const auto dar = std::get_if<std::uint8_t>(&v.application)) {
                        if (v.encrypted) detail::invalid(1, "encrypted DAR");
                        w.u8(2);
                        w.u8(*dar);
                    } else {
                        w.u8(v.encrypted ? 1 : 0);
                        f::octets(w, std::get<Bytes>(v.application));
                    }
                    w.u8(v.verification ? 1 : 0);
                    if (v.verification) {
                        w.u8(static_cast<std::uint8_t>(v.verification->index()));
                        std::visit(
                            [&](const auto& verify) {
                                if constexpr (std::is_same_v<std::decay_t<decltype(verify)>,
                                                             model::Mac>)
                                    f::octets(w, verify.value);
                                else
                                    f::sid_mac(w, verify);
                            },
                            *v.verification);
                    }
                }
            },
            message);
        return w.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
