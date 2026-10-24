#pragma once
#include <string>

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
/** @brief 使用随机盐及 PBKDF2-HMAC-SHA256 编码密码。
 * @param[in] password 密码，最多 1024 字节。
 * @return 算法、迭代次数、盐和派生值；不包含明文。
 */
std::string hash_password(const std::string& password);
/** @brief 恒定时间比较派生密码。
 * @param[in] password 输入密码。
 * @param[in] encoded PBKDF2 编码。
 * @return 格式与派生值是否正确。
 */
bool verify_password(const std::string& password, const std::string& encoded);
/** @brief 恒定时间比较相同长度字节。
 * @param[in] a 第一字符串。
 * @param[in] b 第二字符串。
 * @return 是否相同。
 */
bool secure_equal(const std::string& a, const std::string& b);
}  // namespace ems::web
