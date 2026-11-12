/** @file mutation.hpp
 * @brief SET/ACTION 普通与列表请求、响应及编解码。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu {
struct SetAttribute {
    model::Oad attribute;
    model::Data value;
};

struct SetResult {
    model::Oad attribute;
    std::uint8_t dar = 0;
};

struct SetRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<SetAttribute> attributes;
    std::optional<TimeTag> time_tag;
};

struct SetResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<SetResult> attributes;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct ActionMethod {
    model::Omd method;
    model::Data parameter;
};

struct ActionResult {
    model::Omd method;
    std::uint8_t dar = 0;
    std::optional<model::Data> data;  ///< 存在 NULL Data 与没有返回数据是不同结果。
};

struct ActionRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<ActionMethod> methods;
    std::optional<TimeTag> time_tag;
};

struct ActionResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<ActionResult> methods;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

using MutationApdu = std::variant<SetRequest, SetResponse, ActionRequest, ActionResponse>;
/** @brief 解码一个完整 SET/ACTION APDU。
 * @param[in] bytes 不含链路封装的完整消息。
 * @param[in] limits 字节、列表项数及单个 Data 树预算。
 * @return 拥有内存的精确消息或错误。
 */
DLT698_API Result<MutationApdu> decode_mutation(ByteView bytes, const Limits& limits = {});
/** @brief 编码 SET/ACTION 普通或列表消息。
 * @param[in] message 普通形式须恰好一项，列表形式须非空。
 * @param[in] limits 输出字节、列表项数及单个 Data 树预算。
 * @return APDU 字节或非法字段、资源超限错误。
 */
DLT698_API Result<Bytes> encode_mutation(const MutationApdu& message, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
