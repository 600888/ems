#pragma once

#include <string>

namespace ems::codec {
/**
 * @brief 计算稳定 SHA-256，用于内部序列命名、样本身份及载荷校验。
 * @param[in] input 原始字节串，不执行字符编码或规范化。
 * @return 64 个小写十六进制字符。
 */
std::string sha256(const std::string& input);
}  // namespace ems::codec
