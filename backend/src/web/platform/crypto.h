#pragma once
#include <string>
#include <vector>

namespace ems::web {
/** @brief 计算 SHA-256。
 * @param[in] value 原始字节。
 * @return 小写十六进制摘要。
 */
std::string sha256(const std::string& value);
/** @brief 从系统密码学随机源生成令牌。
 * @return 32 字节随机值的小写十六进制编码。
 */
std::string random_token();
/** @brief 使用随机盐及 Argon2id（64 MiB、3 轮、1 lane）编码密码。
 * @param[in] password 密码，最多 1024 字节。
 * @return PHC 格式算法、成本、盐和派生值；容量耗尽抛 ApiError。
 */
std::string hash_password(const std::string& password);
/** @brief 恒定时间比较派生密码。
 * @param[in] password 输入密码。
 * @param[in] encoded Argon2id 或明确标识的旧 PBKDF2 编码。
 * @return 格式与派生值是否正确；并发容量超限抛 ApiError。
 */
bool verify_password(const std::string& password, const std::string& encoded);
/** @brief 恒定时间比较相同长度字节。
 * @param[in] a 第一字符串。
 * @param[in] b 第二字符串。
 * @return 是否相同。
 */
bool secure_equal(const std::string& a, const std::string& b);
/** @brief 严格解码小写十六进制。
 * @param[in] value 编码。 @return 原始字节，非法格式抛 invalid_argument。
 */
std::vector<char> digest_bytes(const std::string& value);
/** @brief 生成 32 字节随机值的 base64url 编码。 @return 43 字符、无填充 CSRF。 */
std::string random_csrf();
/** @brief 计算 HMAC-SHA256，用于不可伪造的编辑摘要。
 * @param[in] key 共享密钥。 @param[in] value 规范编码。 @return 十六进制摘要。
 */
std::string hmac_sha256(const std::string& key, const std::string& value);
}  // namespace ems::web
