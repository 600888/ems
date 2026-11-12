/** @file md5.hpp
 * @brief 用于 GET MD5 属性一致性校验的摘要算法。
 */
#pragma once
#include <array>
#include <dlt698/common/bytes.hpp>

namespace dlt698 {
/** @brief 计算 RFC 1321 的 128 位摘要。
 * @param[in] bytes 使用期间有效的输入视图，不被保存。
 * @return 按线上 octet-string 顺序排列的 16 字节摘要。
 * @note 此函数仅用于协议属性摘要，不用于安全认证或消息鉴别。
 */
DLT698_API std::array<std::uint8_t, 16> md5(ByteView bytes);
}  // namespace dlt698
