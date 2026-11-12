#pragma once

#include <cstddef>
#include <string_view>

namespace ems::database {
inline constexpr std::size_t kMaxSeriesKeyLength = 192;

/**
 * @brief 校验层级编码；保留冒号作为层级分隔符，不接受空编码或 SQL 引用字符。
 * @param[in] code 设备或测点编码，支持 ASCII 字母、数字、下划线、短横线和点。
 * @return 编码为 1–64 字节且字符合法时返回 true。
 */
inline bool valid_series_code(std::string_view code) {
    if (code.empty() || code.size() > 64) return false;
    for (const char ch : code) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.'))
            return false;
    }
    return true;
}

/**
 * @brief 校验从站根到测点的冒号分隔子表名，不截断、不散列编码。
 * @param[in] key 至少含站编码和测点编码的完整路径，不包含 SQL 反引号。
 * @return 路径不超过 192 字节、层级不超过 9 个设备加 1 个测点且各段合法时返回 true。
 */
inline bool valid_series_key(std::string_view key) {
    if (key.size() > kMaxSeriesKeyLength) return false;
    std::size_t start = 0;
    std::size_t segments = 0;
    for (;;) {
        const auto end = key.find(':', start);
        if (!valid_series_code(
                key.substr(start, end == std::string_view::npos ? end : end - start)))
            return false;
        if (++segments > 10) return false;
        if (end == std::string_view::npos) return segments >= 2;
        start = end + 1;
    }
}
}  // namespace ems::database
