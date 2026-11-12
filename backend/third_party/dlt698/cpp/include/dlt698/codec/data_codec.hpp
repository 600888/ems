/**
 * @file data_codec.hpp
 * @brief 精确类型 Data 的 A-XDR 编解码及组合读写接口。
 */
#pragma once
#include <dlt698/model/data.hpp>

namespace dlt698::codec {
/**
 * @brief 从完整输入解码一个带类型标签的 Data。
 * @param[in] input 仅包含一个 Data 的输入字节，不允许尾随数据。
 * @param[in] limits 输入字节数、节点总数及嵌套深度上限。
 * @return 拥有独立内存的 Data，或包含失败偏移及字段上下文的错误。
 * @note DecodeFailure 在本接口内转换为 Result 错误。
 */
DLT698_API Result<model::Data> decode_data(ByteView input, const Limits& limits = {});
/**
 * @brief 编码一个带类型标签的 Data。
 * @param[in] value 待编码的精确类型数据。
 * @param[in] limits 输出字节数、节点总数及嵌套深度上限。
 * @return 编码后的字节序列，或数据非法、资源超限等错误。
 */
DLT698_API Result<Bytes> encode_data(const model::Data& value, const Limits& limits = {});
/**
 * @brief 读取采用最短编码形式的 A-XDR 长度字段。
 * @param[in,out] reader 从当前位置读取并推进游标的读取器。
 * @param[in] limit 长度值的最大允许值，单位由调用方决定。
 * @return 解码后的长度。
 * @throws DecodeFailure 输入不足、长度形式非法或长度值超过 limit。
 */
DLT698_API std::size_t read_length(Reader& reader, std::size_t limit);
/**
 * @brief 写入采用最短编码形式的 A-XDR 长度字段。
 * @param[in,out] writer 追加编码结果的写入器。
 * @param[in] length 待编码长度，单位由调用方决定。
 * @throws DecodeFailure 写入器输出超限。
 */
DLT698_API void write_length(Writer& writer, std::size_t length);
/**
 * @brief 从当前位置读取一个 Data，保留其后的字节供组合解析。
 * @param[in,out] reader 输入读取器。
 * @param[in] limits 数据字段长度、单个 Data 树节点总数及深度上限。
 * @param[in] depth 当前根节点的起始嵌套深度，通常为零。
 * @return 解码后的精确类型数据。
 * @throws DecodeFailure 输入不足、标签不支持、数据非法或资源超限。
 * @note 输入总长度由调用方限制；节点预算在每次 read_data 调用时重新计数。
 */
DLT698_API model::Data read_data(Reader& reader, const Limits& limits, std::size_t depth = 0);
/**
 * @brief 向现有输出追加一个带类型标签的 Data。
 * @param[in,out] writer 输出写入器，其字节上限由创建时决定。
 * @param[in] value 待编码的精确类型数据。
 * @param[in] limits 数据字段长度、单个 Data 树节点总数及深度上限。
 * @param[in] depth 当前根节点的起始嵌套深度，通常为零。
 * @throws DecodeFailure 数据非法或资源超限。
 * @note 失败时不会回滚已追加的字节；节点预算在每次调用时重新计数。
 */
DLT698_API void write_data(Writer& writer, const model::Data& value, const Limits& limits,
                           std::size_t depth = 0);
/**
 * @brief 读取四字节 OAD 内容，不读取 Data 类型标签。
 * @param[in,out] reader 输入读取器。
 * @return 包含 OI、完整属性字节和索引的对象属性描述符。
 * @throws DecodeFailure 输入不足。
 */
DLT698_API model::Oad read_oad(Reader& reader);
/**
 * @brief 写入四字节 OAD 内容，不附加 Data 类型标签。
 * @param[in,out] writer 输出写入器。
 * @param[in] value 对象属性描述符，属性字节的特征位原样保留。
 * @throws DecodeFailure 写入器输出超限。
 */
DLT698_API void write_oad(Writer& writer, const model::Oad& value);
}  // namespace dlt698::codec
