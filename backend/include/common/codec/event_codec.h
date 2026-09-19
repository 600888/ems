/**
 * @file event_codec.h
 * @brief 事件信封编解码（design.md 第 10.1 节）
 *
 * type/schema 不支持时必须隔离并告警，不能反序列化为默认值继续
 * 执行：解码返回 SchemaMismatch 错误。重放/转发保存原始载荷。
 */
#pragma once

#include <string>

#include <common/core/result.h>
#include <common/messaging/event.h>

namespace ems::codec {

/// EventEnvelope → JSON（包含信封字段与版本化载荷）
core::Result<std::string> encode_event(const messaging::EventEnvelope& event);

/// JSON → EventEnvelope；未知 type/schema_version 返回 SchemaMismatch
core::Result<messaging::EventEnvelope> decode_event(const std::string& json_text);

}  // namespace ems::codec
