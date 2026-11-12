/** @file security.hpp
 * @brief SECURITY 外层安全传输封装；不执行任何密码学操作。
 */
#pragma once
#include <dlt698/codec/data_codec.hpp>
#include <optional>

namespace dlt698::protocol::apdu {
struct RnMac {
    model::Rn random;
    model::Mac mac;
};

struct SecurityRequest {
    bool encrypted = false;
    Bytes application;
    std::variant<model::SidMac, model::Rn, RnMac, model::Sid> verification;
};

struct SecurityResponse {
    bool encrypted = false;                         ///< DAR 分支忽略此值；编码时要求为 false。
    std::variant<Bytes, std::uint8_t> application;  ///< Bytes 明文/密文，uint8_t 为异常 DAR。
    std::optional<std::variant<model::Mac, model::SidMac>> verification;
};

using SecurityApdu = std::variant<SecurityRequest, SecurityResponse>;
/** @brief 解码 10H/90H 安全封装全部分支。
 * @param[in] bytes 完整外层 APDU；该封装没有 PIID、FollowReport 或 TimeTag 尾部。
 * @param[in] limits 全消息和可变字段的字节上限。
 * @return 拥有型安全消息或格式/资源错误，不验证 MAC、随机数或密文。
 */
DLT698_API Result<SecurityApdu> decode_security(ByteView bytes, const Limits& limits = {});
/** @brief 编码安全封装，保持明文/密文和验证信息 CHOICE。
 * @param[in] message 完整拥有型消息。
 * @param[in] limits 全消息字节上限。
 * @return 外层 APDU 或格式/资源错误。
 */
DLT698_API Result<Bytes> encode_security(const SecurityApdu& message, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
