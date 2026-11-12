/** @file advanced.hpp
 * @brief ThenGet、REPORT 与七类 PROXY 的拥有型消息模型。
 */
#pragma once
#include <dlt698/protocol/apdu/mutation.hpp>

namespace dlt698::protocol::apdu {
struct SetThenGet {
    SetAttribute set;
    model::Oad read;
    std::uint8_t delay_seconds = 0;  ///< 零采用目标服务器默认延时。
};

struct ActionThenGet {
    ActionMethod action;
    model::Oad read;
    std::uint8_t delay_seconds = 0;
};

struct SetThenGetResult {
    SetResult set;
    AttributeResult read;
};

struct ActionThenGetResult {
    ActionResult action;
    AttributeResult read;
};

struct SetThenGetRequest {
    std::uint8_t piid = 0;
    std::vector<SetThenGet> items;
    std::optional<TimeTag> time_tag;
};

struct SetThenGetResponse {
    std::uint8_t piid_acd = 0;
    std::vector<SetThenGetResult> items;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ActionThenGetRequest {
    std::uint8_t piid = 0;
    std::vector<ActionThenGet> items;
    std::optional<TimeTag> time_tag;
};

struct ActionThenGetResponse {
    std::uint8_t piid_acd = 0;
    std::vector<ActionThenGetResult> items;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct TransData {
    model::Oad port;
    std::vector<Bytes> data;
};

struct ReportNotification {
    std::uint8_t piid_acd = 0;
    std::variant<std::vector<AttributeResult>, std::vector<RecordResult>, TransData> payload;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ReportResponse {
    std::uint8_t piid = 0;
    std::uint8_t choice = 1;  ///< 1 普通，2 记录，3 透明；透明确认不带描述符。
    std::vector<model::Oad> attributes;
    std::optional<TimeTag> time_tag;
};

template <class T>
struct ProxyTarget {
    model::Tsa server;
    std::uint16_t timeout_seconds = 0;  ///< 请求专用，零使用代理默认值；响应不编码。
    std::vector<T> items;
};

struct ProxyRecordRequest {
    model::Tsa server;
    GetRecord record;
};

struct ProxyRecordResponse {
    model::Tsa server;
    RecordResult record;
};

struct ProxyTransRequest {
    model::Oad port;
    model::Comdcb communication;
    std::uint16_t response_timeout_seconds = 0;
    std::uint16_t byte_timeout_milliseconds = 0;
    Bytes command;
};

struct ProxyTransResponse {
    model::Oad port;
    std::variant<std::uint8_t, Bytes> result;
};

using ProxyRequestPayload =
    std::variant<std::vector<ProxyTarget<model::Oad>>, ProxyRecordRequest,
                 std::vector<ProxyTarget<SetAttribute>>, std::vector<ProxyTarget<SetThenGet>>,
                 std::vector<ProxyTarget<ActionMethod>>, std::vector<ProxyTarget<ActionThenGet>>,
                 ProxyTransRequest>;
using ProxyResponsePayload =
    std::variant<std::vector<ProxyTarget<AttributeResult>>, ProxyRecordResponse,
                 std::vector<ProxyTarget<SetResult>>, std::vector<ProxyTarget<SetThenGetResult>>,
                 std::vector<ProxyTarget<ActionResult>>,
                 std::vector<ProxyTarget<ActionThenGetResult>>, ProxyTransResponse>;

struct ProxyRequest {
    std::uint8_t piid = 0;
    std::uint16_t timeout_seconds = 1;  ///< 分支 1 至 6 的总超时，必须非零；透明转发不编码。
    ProxyRequestPayload payload;
    std::optional<TimeTag> time_tag;
};

struct ProxyResponse {
    std::uint8_t piid_acd = 0;
    ProxyResponsePayload payload;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

using AdvancedApdu =
    std::variant<SetThenGetRequest, SetThenGetResponse, ActionThenGetRequest, ActionThenGetResponse,
                 ReportNotification, ReportResponse, ProxyRequest, ProxyResponse>;
/** @brief 解码 ThenGet、REPORT 或 PROXY 的完整消息。
 * @param[in] bytes 完整 APDU，不含链路封装。
 * @param[in] limits 字节、列表和每个 Data 的上限。
 * @return 拥有型消息或格式/资源错误；未知服务返回 unsupported_service。
 */
DLT698_API Result<AdvancedApdu> decode_advanced(ByteView bytes, const Limits& limits = {});
/** @brief 编码高级服务并检查字段及非空列表。
 * @param[in] message 拥有型消息。
 * @param[in] limits 输出与列表资源上限。
 * @return APDU 字节或格式/资源错误。
 */
DLT698_API Result<Bytes> encode_advanced(const AdvancedApdu& message, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
