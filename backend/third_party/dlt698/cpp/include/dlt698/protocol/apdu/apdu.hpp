/** @file apdu.hpp
 * @brief 当前已支持 APDU 的统一分发入口。
 */
#pragma once
#include <dlt698/protocol/apdu/advanced.hpp>
#include <dlt698/protocol/apdu/connection.hpp>

namespace dlt698::protocol::apdu {
using Apdu =
    std::variant<LinkRequest, LinkResponse, ConnectRequest, ConnectResponse, ReleaseRequest,
                 ReleaseResponse, ReleaseNotification, ErrorResponse, GetRequest, GetResponse,
                 SetRequest, SetResponse, ActionRequest, ActionResponse, GetRecordRequest,
                 GetRecordResponse, GetNextRequest, GetNextResponse, GetMd5Request, GetMd5Response,
                 SetThenGetRequest, SetThenGetResponse, ActionThenGetRequest, ActionThenGetResponse,
                 ReportNotification, ReportResponse, ProxyRequest, ProxyResponse>;
/** @brief 解码当前支持的一个完整 APDU。
 * @param[in] bytes 完整 APDU 字节。
 * @param[in] limits 编解码资源上限。
 * @return 精确类型消息或不支持、语法及资源错误。
 */
DLT698_API Result<Apdu> decode_apdu(ByteView bytes, const Limits& limits = {});
/** @brief 编码当前支持的一个完整 APDU。
 * @param[in] message 精确类型消息。
 * @param[in] limits 编解码资源上限。
 * @return 编码字节或字段及资源错误。
 */
DLT698_API Result<Bytes> encode_apdu(const Apdu& message, const Limits& limits = {});
/** @brief 查询高级事务对应的 C.1 协商位。
 * @param[in] message MD5、ThenGet、REPORT 或 PROXY 请求。
 * @return 协商位序号；64 表示标准未分配独立能力位；其他消息为 65。
 */
DLT698_API unsigned advanced_capability(const Apdu& message);
/** @brief 校验高级事务的响应分支和完整描述符顺序。
 * @param[in] request 原始请求或通知。
 * @param[in] response 候选响应或确认。
 * @return 分支、TSA、OAD/OMD 与列表顺序完全匹配时为 true；PIID/TimeTag 由会话检查。
 */
DLT698_API bool advanced_matches(const Apdu& request, const Apdu& response);
}  // namespace dlt698::protocol::apdu
