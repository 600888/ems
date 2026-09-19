/**
 * @file model_codec.h
 * @brief 模型与契约之间的显式编解码（design.md 第 2/5.3 节）
 *
 * JSON 实现为私有实现（nlohmann），公开 API 只使用 std::string，
 * 业务头文件不暴露第三方类型。模型不继承 to_json/from_json 基类，
 * 序列化由 codec 承担。
 *
 * 编码规则（5.3）：
 * - 所有 64 位整数/ID 在 JSON 中使用十进制字符串；
 * - PointValue 类型由 ValueType 声明，编码器校验类型与值的组合；
 * - 禁止 NaN/Inf；业务时间为 UTC Unix 微秒 int64（同样编码为十进制字符串）。
 */
#pragma once

#include <string>

#include <common/core/result.h>
#include <common/model/asset.h>
#include <common/model/binding.h>
#include <common/model/device.h>
#include <common/model/point.h>

namespace ems::codec {

/// 类型化值 → JSON 文本（非法组合返回 InvalidArgument，如 NaN/Inf）
core::Result<std::string> encode_point_value(model::ValueType type,
                                             const model::PointValue& value);

/// JSON 文本 → 类型化值（类型不符返回 SchemaMismatch）
core::Result<model::PointValue> decode_point_value(model::ValueType type,
                                                   const std::string& json_text);

/// AssetNode ↔ JSON
core::Result<std::string> encode_asset(const model::AssetNode& asset);
core::Result<model::AssetNode> decode_asset(const std::string& json_text);

/// Device ↔ JSON
core::Result<std::string> encode_device(const model::Device& device);
core::Result<model::Device> decode_device(const std::string& json_text);

/// Point ↔ JSON
core::Result<std::string> encode_point(const model::Point& point);
core::Result<model::Point> decode_point(const std::string& json_text);

/// DeviceChannelBinding ↔ JSON
core::Result<std::string> encode_binding(const model::DeviceChannelBinding& binding);
core::Result<model::DeviceChannelBinding> decode_binding(const std::string& json_text);

/// VendorPointRoute ↔ JSON
core::Result<std::string> encode_vendor_route(const model::VendorPointRoute& route);
core::Result<model::VendorPointRoute> decode_vendor_route(const std::string& json_text);

/// ForwardingPointMap ↔ JSON
core::Result<std::string> encode_forwarding_map(const model::ForwardingPointMap& map);
core::Result<model::ForwardingPointMap> decode_forwarding_map(const std::string& json_text);

}  // namespace ems::codec
