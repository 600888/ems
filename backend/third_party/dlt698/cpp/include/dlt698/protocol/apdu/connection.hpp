/**
 * @file connection.hpp
 * @brief LINK、CONNECT、RELEASE 和异常响应的值模型及编解码。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu {
/// 由协议服务器发起的预连接操作，与 TCP 的连接发起方向无关。
enum class LinkRequestType : std::uint8_t { login = 0, heartbeat = 1, logout = 2 };

struct LinkRequest {
    std::uint8_t piid_acd = 0;
    LinkRequestType type = LinkRequestType::login;
    std::uint16_t heartbeat_seconds = 180;
    model::DateTime requested_at;
};

struct LinkResponse {
    std::uint8_t piid = 0;
    std::uint8_t result = 0;  ///< bit7 为时钟可信标志，低三位为结果（0 至 3）。
    model::DateTime requested_at;
    model::DateTime received_at;
    model::DateTime responded_at;
};

/// 固定长度的厂商字段，不包含 A-XDR 长度或 Data 标签，扩展字段允许零填充。
struct FactoryVersion {
    std::array<std::uint8_t, 4> manufacturer{};
    std::array<std::uint8_t, 4> software_version{};
    std::array<std::uint8_t, 6> software_date{};
    std::array<std::uint8_t, 4> hardware_version{};
    std::array<std::uint8_t, 6> hardware_date{};
    std::array<std::uint8_t, 8> extension{};
};

/// 帧尺寸是长度域 L 的上限，不包含 0x68/0x16；APDU 尺寸不包含链路封装。
struct AssociationParameters {
    std::uint16_t version = 0x0010;
    std::array<std::uint8_t, 8> protocol{};  ///< 一致性位图按最高位对应序号零。
    std::array<std::uint8_t, 16> function{};
    std::uint16_t send_frame_bytes = 1024;
    std::uint16_t receive_frame_bytes = 1024;
    std::uint8_t receive_window = 1;
    std::uint16_t apdu_bytes = 1024;
    std::uint32_t timeout_seconds = 100;
};

struct NullSecurity {};

struct PasswordSecurity {
    std::string password;
};

struct SymmetrySecurity {
    Bytes ciphertext;
    Bytes signature;
};

struct SignatureSecurity {
    Bytes ciphertext;
    Bytes signature;
};

using ConnectMechanism =
    std::variant<NullSecurity, PasswordSecurity, SymmetrySecurity, SignatureSecurity>;

struct SecurityData {
    Bytes random;
    Bytes signature;
};

/// 仅保存认证报文的线格式，编解码本身不会执行密码验证或认证。
struct ConnectRequest {
    std::uint8_t piid = 0;
    AssociationParameters parameters;
    ConnectMechanism mechanism = NullSecurity{};
    std::optional<TimeTag> time_tag;
};

struct ConnectResponse {
    std::uint8_t piid_acd = 0;
    FactoryVersion factory;
    AssociationParameters parameters;
    std::uint8_t result = 0;  ///< 0 至 5 或 255，保留远端认证结果原码。
    std::optional<SecurityData> security;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ReleaseRequest {
    std::uint8_t piid = 0;
    std::optional<TimeTag> time_tag;
};

struct ReleaseResponse {
    std::uint8_t piid_acd = 0;
    std::uint8_t result = 0;  ///< 标准仅定义成功（0）。
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ReleaseNotification {
    std::uint8_t piid_acd = 0;
    model::DateTimeS established_at;
    model::DateTimeS current_time;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ErrorResponse {
    bool server = true;  ///< true 编码为 EE，false 编码为 6E。
    std::uint8_t piid = 0;
    std::uint8_t type = 2;  ///< 1=无法解析，2=服务不支持，255=其他。
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

using ConnectionApdu =
    std::variant<LinkRequest, LinkResponse, ConnectRequest, ConnectResponse, ReleaseRequest,
                 ReleaseResponse, ReleaseNotification, ErrorResponse>;
/**
 * @brief 解码恰好一个连接管理或异常响应 APDU。
 * @param[in] bytes 完整 APDU，不含链路封装。
 * @param[in] limits 输入及可变认证字段的字节上限。
 * @return 精确类型消息或含偏移的错误。
 */
DLT698_API Result<ConnectionApdu> decode_connection(ByteView bytes, const Limits& limits = {});
/**
 * @brief 编码连接管理或异常响应 APDU。
 * @param[in] message 待编码消息，日历字段按原始字节保存。
 * @param[in] limits 输出字节上限。
 * @return APDU 字节或字段非法、资源超限错误。
 */
DLT698_API Result<Bytes> encode_connection(const ConnectionApdu& message,
                                           const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
