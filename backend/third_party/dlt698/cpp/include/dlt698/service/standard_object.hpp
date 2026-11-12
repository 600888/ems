/**
 * @file standard_object.hpp
 * @brief 标准点位的只读 schema 生成和严格 provider 绑定。
 */
#pragma once
#include <dlt698/service/object.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::service {
/**
 * @brief 为应用明确选择的标准属性生成只读 schema，不启用可选方法。
 * @param[in] oi 本批目录中的 OI。
 * @param[in] attributes 非空且不重复的属性编号列表，不包含特征位。
 * @return 拥有名称及属性列表的 schema；未知定义/非法选择返回错误。
 * @note 手写 schema 仍可用于厂家扩展；此接口不会修改对象目录。
 */
DLT698_API Result<ObjectSchema> make_object_schema(std::uint16_t oi,
                                                   const std::vector<std::uint8_t>& attributes);
/**
 * @brief 通过只读校验适配器把选定标准属性绑定到对象目录。
 * @param[in,out] registry 目标目录，重复 OI 拒绝且不覆盖已有 provider。
 * @param[in] oi 本批标准 OI。
 * @param[in] attributes 明确选择的非空属性编号，不重复；未选属性保持未定义。
 * @param[in] provider 非空共享 provider，不自动生成逻辑名、单位或硬件值。
 * @param[in] layout 本地数组配置，必须与 provider 的完整数组一致。
 * @param[in] limits 每次返回值校验的深度、元素和字节上限。
 * @return 成功或配置/定义/重复注册错误。
 * @note 目录通过适配器持有 provider；转发只读 read/read_record，原始 OAD/RSD/RCSD 不改写。provider 须满足原接口并发和执行器合约。
 * @note 非零特征返回 DAR=3，非法索引为 8，Data 不符合标准或超限为 7；provider 的 DAR 原样返回，异常由目录隔离为 255。
 * @note 记录请求超出编码资源预算返回 DAR=3；响应投影不符合表头/类型/资源约定返回 7。
 */
DLT698_API Result<void> register_standard_object(ObjectRegistry& registry, std::uint16_t oi,
                                                 const std::vector<std::uint8_t>& attributes,
                                                 std::shared_ptr<IObjectProvider> provider,
                                                 const standard::DeviceLayout& layout = {},
                                                 const Limits& limits = {});
}  // namespace dlt698::service
