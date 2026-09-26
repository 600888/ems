/**
 * @file event_codec.h
 * @brief 事件信封编解码（design.md 第 10.1 节）
 *
 * type/schema 不支持时必须隔离并告警，不能反序列化为默认值继续
 * 执行：解码返回 SchemaMismatch 错误。重放/转发保存原始载荷。
 */
#pragma once

#include <common/core/result.h>
#include <common/messaging/event.h>

#include <string>

namespace ems::codec {

/**
 * @brief 将事件信封及版本化载荷编码为 JSON。
 * @param[in] event 事件数据。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_event(const messaging::EventEnvelope& event);

/**
 * @brief 从 JSON 解码事件信封；未知类型或 schema 版本返回 SchemaMismatch。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<messaging::EventEnvelope> decode_event(const std::string& json_text);

}  // namespace ems::codec
