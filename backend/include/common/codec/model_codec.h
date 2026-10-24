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

#include <common/core/result.h>
#include <common/model/binding.h>
#include <common/model/device.h>
#include <common/model/point.h>

#include <string>

namespace ems::codec {

/**
 * @brief 将类型化点值编码为 JSON；非法类型组合或 NaN/Inf 返回 InvalidArgument。
 * @param[in] type 连接或数据类型。
 * @param[in] value 输入值。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_point_value(model::ValueType type, const model::PointValue& value);

/**
 * @brief 从 JSON 解码类型化点值；类型不符返回 SchemaMismatch。
 * @param[in] type 连接或数据类型。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含测点值；失败时包含错误信息。
 */
core::Result<model::PointValue> decode_point_value(model::ValueType type,
                                                   const std::string& json_text);

/**
 * @brief 将设备编码为 JSON。
 * @param[in] device 设备对象。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_device(const model::Device& device);

/**
 * @brief 从 JSON 解码设备。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<model::Device> decode_device(const std::string& json_text);

/**
 * @brief 将点定义编码为 JSON。
 * @param[in] point 测点数据。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_point(const model::PointBase& point);

/**
 * @brief 从 JSON 解码点定义。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<model::AnyPoint> decode_point(const std::string& json_text);

/**
 * @brief 将设备通道绑定编码为 JSON。
 * @param[in] binding 设备与通道的绑定关系。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_binding(const model::DeviceChannelBinding& binding);

/**
 * @brief 从 JSON 解码设备通道绑定。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<model::DeviceChannelBinding> decode_binding(const std::string& json_text);

/**
 * @brief 将厂商点路由编码为 JSON。
 * @param[in] route 路由配置。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_vendor_route(const model::VendorPointRoute& route);

/**
 * @brief 从 JSON 解码厂商点路由。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<model::VendorPointRoute> decode_vendor_route(const std::string& json_text);

/**
 * @brief 将转发点映射编码为 JSON。
 * @param[in] map 键值映射。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<std::string> encode_forwarding_map(const model::ForwardingPointMap& map);

/**
 * @brief 从 JSON 解码转发点映射。
 * @param[in] json_text JSON 文本。
 * @return 成功时包含结果；失败时包含错误信息。
 */
core::Result<model::ForwardingPointMap> decode_forwarding_map(const std::string& json_text);

}  // namespace ems::codec
