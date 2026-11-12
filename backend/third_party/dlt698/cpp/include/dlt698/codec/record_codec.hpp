/** @file record_codec.hpp
 * @brief 无 Data 标签的记录选择器组合编解码。
 */
#pragma once
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/model/record.hpp>

namespace dlt698::codec {
/** @brief 读取完整记录行选择器。
 * @param[in,out] reader 输入游标。
 * @param[in] limits 深度、元素与字节预算。
 * @return 拥有全部选择条件的 RSD。
 * @throws DecodeFailure 分支非法、截断或超限。
 */
DLT698_API model::Rsd read_rsd(Reader& reader, const Limits& limits = {});
/** @brief 写入记录行选择器。
 * @param[in,out] writer 有界输出。
 * @param[in] value 选择器，区间选择为前闭后开，业务含义由 provider 解释。
 * @param[in] limits 资源预算。
 * @throws DecodeFailure 非法分支字段或超限。
 */
DLT698_API void write_rsd(Writer& writer, const model::Rsd& value, const Limits& limits = {});
/** @brief 读取记录列选择器。
 * @param[in,out] reader 输入游标。
 * @param[in] limits 元素预算。
 * @return OAD/ROAD 列序列；空请求表示全选，响应须给出实际表头。
 * @throws DecodeFailure 分支非法、截断或超限。
 */
DLT698_API model::Rcsd read_rcsd(Reader& reader, const Limits& limits = {});
/** @brief 写入记录列选择器。
 * @param[in,out] writer 有界输出。
 * @param[in] value 有序列定义。
 * @param[in] limits 元素预算。
 * @throws DecodeFailure 超出资源预算。
 */
DLT698_API void write_rcsd(Writer& writer, const model::Rcsd& value, const Limits& limits = {});
/** @brief 读取表计集合的全部八种分支。
 * @param[in,out] reader 输入游标。
 * @param[in] limits 元素、Data 深度预算。
 * @return 有类型的表计集合。
 * @throws DecodeFailure 非法类型、区间或超限。
 */
DLT698_API model::Ms read_ms(Reader& reader, const Limits& limits = {});
/** @brief 写入表计集合，区间端点须匹配对应 UInt8/TSA/UInt16 类型。
 * @param[in,out] writer 有界输出。
 * @param[in] value 表计集合。
 * @param[in] limits 元素、Data 深度预算。
 * @throws DecodeFailure 区间类型不符、边界非法或超限。
 */
DLT698_API void write_ms(Writer& writer, const model::Ms& value, const Limits& limits = {});
}  // namespace dlt698::codec
