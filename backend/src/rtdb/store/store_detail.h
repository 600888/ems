#pragma once
#include <rtdb/store/backend_factory.h>

#include <cstdint>
#include <string_view>

namespace ems::rtdb::detail {
constexpr std::size_t max_key_bytes = 2048;
constexpr std::size_t max_fields = 256;
/**
 * @brief 构造带写入确定性的实时存储错误。
 * @param[in] code 错误码。
 * @param[in] effect 写入是否可能已生效。
 * @param[in] message 错误说明。
 * @return 得到的错误对象。
 */
Error error(ErrorCode code, WriteEffect effect, const char* message);

/**
 * @brief 校验字节序列是否为合法 UTF-8。
 * @param[in] bytes 待校验的字节序列。
 * @return true 表示操作成功；false 表示失败。
 */
bool utf8(std::string_view bytes);

/**
 * @brief 校验记录编码，设备编码可使用专属规则。
 * @param[in] code 待校验的记录编码。
 * @param[in] device 是否应用设备编码的保留词规则。
 * @return true 表示条件成立；false 表示不成立。
 */
bool valid_code(std::string_view code, bool device = false);

/**
 * @brief 将逻辑记录键编码为物理存储键。
 * @param[in] key 逻辑记录键。
 * @return 用于存储该记录的物理键。
 */
std::string physical_key(const RecordKey& key);

/**
 * @brief 解析指定作用域的设备状态物理键，忽略测点、临时键和其他作用域。
 * @param[in] scope 逻辑作用域。
 * @param[in] physical 物理存储键。
 * @return 合法设备状态键；不匹配时为空。
 */
std::optional<RecordKey> device_state_key(const Scope& scope, std::string_view physical);

/**
 * @brief 为同一逻辑记录生成 Redis 哈希标签。
 * @param[in] key 逻辑记录键。
 * @return 包含作用域标识的 Redis 哈希标签。
 */
std::string hash_tag(const RecordKey& key);

/**
 * @brief 计算稳定的键哈希值。
 * @param[in] bytes 待计算哈希的字节序列。
 * @return 输入字节序列的稳定 64 位哈希值。
 */
std::uint64_t hash(std::string_view bytes);

/**
 * @brief 计算记录镜像的校验和。
 * @param[in] bytes 待计算校验和的字节序列。
 * @return 输入字节序列的 CRC32 校验和。
 */
std::uint32_t checksum(std::string_view bytes);

/**
 * @brief 生成不可复用的随机版本令牌。
 * @return 成功时包含结果；失败时包含错误信息。
 */
Result<std::string> token();

/**
 * @brief 在大小限制内将字段映射编码为存储字节。
 * @param[in] fields 待编码的字段映射。
 * @param[in] limit 编码结果允许的最大字节数。
 * @param[in] effect 发生错误时报告的写入影响标记。
 * @return 成功时包含结果；失败时包含错误信息。
 */
Result<std::string> encode_fields(const Fields& fields, std::size_t limit, WriteEffect effect);

/**
 * @brief 在大小限制内从存储字节解码字段映射。
 * @param[in] bytes 待解码的存储字节。
 * @param[in] limit 允许的最大字节数。
 * @param[in] effect 发生错误时报告的写入影响标记。
 * @return 成功时包含字段映射；失败时包含错误信息。
 */
Result<Fields> decode_fields(std::string_view bytes, std::size_t limit, WriteEffect effect);

/**
 * @brief 将 32 位无符号整数追加到字节串。
 * @param[in,out] bytes 接收编码结果的字节串。
 * @param[in] value 要追加的整数。
 */
void append_u32(std::string& bytes, std::uint32_t value);

/**
 * @brief 从字节视图读取并移除一个 32 位无符号整数。
 * @param[in,out] bytes 输入视图；成功后移除已读取的字节。
 * @param[out] value 解码得到的整数。
 * @return true 表示操作成功；false 表示失败。
 */
bool take_u32(std::string_view& bytes, std::uint32_t& value);

/**
 * @brief 将长度前缀字符串追加到字节串。
 * @param[in,out] bytes 接收编码结果的字节串。
 * @param[in] value 要追加的字符串。
 */
void append_string(std::string& bytes, std::string_view value);

/**
 * @brief 从字节视图读取并移除一个长度前缀字符串。
 * @param[in,out] bytes 输入视图；成功后移除已读取的字节。
 * @param[out] value 解码得到的字符串。
 * @return true 表示操作成功；false 表示失败。
 */
bool take_string(std::string_view& bytes, std::string& value);

/**
 * @brief 将非负十进制文本解析为容量值。
 * @param[in] text 待解析的十进制文本。
 * @param[out] value 解析得到的容量值。
 * @return true 表示操作成功；false 表示失败。
 */
bool parse_size(const std::string& text, std::size_t& value);

/**
 * @brief 校验后端规格仅包含允许的选项名。
 * @param[in] spec 待校验的后端规格。
 * @param[in] keys 允许使用的选项名列表。
 * @return true 表示操作成功；false 表示失败。
 */
bool known_options(const BackendSpec& spec, std::initializer_list<const char*> keys);
}  // namespace ems::rtdb::detail
