/**
 * @file get.hpp
 * @brief GET 普通、列表、记录及自解析分块的请求、响应与 APDU 编解码。
 */
#pragma once
#include <dlt698/codec/record_codec.hpp>
#include <optional>

namespace dlt698::protocol::apdu {
/// 可选时间标签，日期时间保持协议原始字段，不附加本机时区。
struct TimeTag {
    model::DateTimeS sent_at;  ///< 发送时间。
    model::Ti allowed_delay;   ///< 允许的传输延时。

    /** @brief 比较时间标签的所有线上字段。
     * @param[in] a 左侧标签。
     * @param[in] b 右侧标签。
     * @return 时标和允许延时相同时为 true。
     */
    friend bool operator==(const TimeTag& a, const TimeTag& b) {
        return a.sent_at == b.sent_at && a.allowed_delay == b.allowed_delay;
    }
};

/// GET 请求，Normal 恰含一个属性，NormalList 必须为非空列表。
struct GetRequest {
    std::uint8_t piid = 0;  ///< 优先级位及低六位调用标识，第 6 位保留且须为零。
    bool list = false;
    std::vector<model::Oad> attributes;
    std::optional<TimeTag> time_tag;
};

/// 单个属性的读取结果，DAR 与精确类型 Data 分开保存。
struct AttributeResult {
    model::Oad attribute;
    std::variant<std::uint8_t, model::Data> result;  ///< uint8_t 为 DAR，Data 为属性值。
};

/** @brief 完整记录查询；行条件由 provider 解释，空 columns 表示全选。 */
struct GetRecord {
    model::Oad attribute;
    model::Rsd rows;
    model::Rcsd columns;
};

struct GetRecordRequest {
    std::uint8_t piid = 0;
    bool list = false;
    std::vector<GetRecord> records;
    std::optional<TimeTag> time_tag;
};

using RecordRow = std::vector<model::Data>;

/** @brief 拥有表头与行数据的快照；每行 Data 数量等于 columns 数量，ROAD 列也占一个 Data。 */
struct RecordResult {
    model::Oad attribute;
    model::Rcsd columns;
    std::variant<std::uint8_t, std::vector<RecordRow>> result;
};

/// 跟随上报拥有独立数据，普通结果与记录结果对应 CHOICE 1/2。
using FollowReport = std::variant<std::vector<AttributeResult>, std::vector<RecordResult>>;

struct GetResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<AttributeResult> attributes;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct GetRecordResponse {
    std::uint8_t piid_acd = 0;
    bool list = false;
    std::vector<RecordResult> records;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct GetNextRequest {
    std::uint8_t piid = 0;
    std::uint16_t block = 0;  ///< 最近正确接收的块号，保持原请求 PIID/优先级。
    std::optional<TimeTag> time_tag;
};

struct GetNextResponse {
    std::uint8_t piid_acd = 0;
    bool last = false;
    std::uint16_t block = 0;
    std::variant<std::uint8_t, std::vector<AttributeResult>, std::vector<RecordResult>> result;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

struct GetMd5Request {
    std::uint8_t piid = 0;
    model::Oad attribute;
    std::optional<TimeTag> time_tag;
};

struct GetMd5Response {
    std::uint8_t piid_acd = 0;
    model::Oad attribute;
    std::variant<std::uint8_t, std::array<std::uint8_t, 16>> result;
    std::optional<TimeTag> time_tag;
    std::optional<FollowReport> follow_report{};
};

using GetApdu = std::variant<GetRequest, GetResponse, GetRecordRequest, GetRecordResponse,
                             GetNextRequest, GetNextResponse, GetMd5Request, GetMd5Response>;
/**
 * @brief 解码完整的 GET 请求或响应 APDU。
 * @param[in] bytes 恰好一个 APDU，不包含链路帧封装或尾随字节。
 * @param[in] limits APDU 字节数、属性数及每个 Data 树的资源上限。
 * @return GET 请求或响应；未知服务或变体返回 unsupported_service。
 * @note 也检查 PIID 保留位、属性结果选择符和可选时间标签的合法性。
 */
DLT698_API Result<GetApdu> decode_get(ByteView bytes, const Limits& limits = {});
/**
 * @brief 编码 GET 普通、列表、记录、Next 或 MD5 请求/响应。
 * @param[in] apdu 普通/记录非列表恰含一项，列表非空；Next 错误块必须为末块。
 * @param[in] limits 输出字节数、属性数及每个 Data 树的资源上限。
 * @return APDU 字节序列，或属性数、字段值非法及资源超限等错误。
 * @note MD5 对编码后的属性 Data 计算，摘要固定为 16 字节。
 */
DLT698_API Result<Bytes> encode_get(const GetApdu& apdu, const Limits& limits = {});
}  // namespace dlt698::protocol::apdu
