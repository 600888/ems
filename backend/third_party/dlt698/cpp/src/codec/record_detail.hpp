/** @file record_detail.hpp
 * @brief 描述符与 Data 相互嵌套时共用整个树的深度和节点预算。
 */
#pragma once
#include <dlt698/model/record.hpp>

namespace dlt698::codec::detail {
/** @brief 按共享预算读取嵌套 Data。
 * @param[in,out] r 读取器。
 * @param[in] l 限制。
 * @param[in] depth 当前深度。
 * @param[in,out] nodes 全树节点数。
 * @return 精确 Data。
 * @throws DecodeFailure 截断、非法或超限。
 */
model::Data read_nested(Reader& r, const Limits& l, std::size_t depth, std::size_t& nodes);
/** @brief 按共享预算写入嵌套 Data。
 * @param[in,out] w 写入器。
 * @param[in] v 数据。
 * @param[in] l 限制。
 * @param[in] depth 当前深度。
 * @param[in,out] nodes 全树节点数。
 * @throws DecodeFailure 字段非法或超限。
 */
void write_nested(Writer& w, const model::Data& v, const Limits& l, std::size_t depth,
                  std::size_t& nodes);
/** @brief 读取不含外层 Data 标签的记录描述符。
 * @param[in,out] r 读取器。
 * @param[in] type 已读取的 Data 标签。
 * @param[in] l 限制。
 * @param[in] depth 当前深度。
 * @param[in,out] nodes 共享节点数。
 * @return 不可变有类型描述符。
 * @throws DecodeFailure 字段非法或超限。
 */
model::RecordData read_descriptor(Reader& r, model::DataType type, const Limits& l,
                                  std::size_t depth, std::size_t& nodes);
/** @brief 写入不含外层 Data 标签的记录描述符。
 * @param[in,out] w 写入器。
 * @param[in] v 不可变描述符。
 * @param[in] l 限制。
 * @param[in] depth 当前深度。
 * @param[in,out] nodes 共享节点数。
 * @throws DecodeFailure 字段非法或超限。
 */
void write_descriptor(Writer& w, const model::RecordData& v, const Limits& l, std::size_t depth,
                      std::size_t& nodes);
}  // namespace dlt698::codec::detail
