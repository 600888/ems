#include <time.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace ems {

/// @brief string转小写
/// @param[in] str 输入字符串。
/// @return 对应的文本内容。
inline std::string to_lower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower;
}

/// @brief string转大写
/// @param[in] str 输入字符串。
/// @return 对应的文本内容。
inline std::string to_upper(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper;
}

/// @brief 比较两个 string 不区分大小写
/// @param[in] a 左侧输入值。
/// @param[in] b 右侧输入值。
/// @return true 表示操作成功；false 表示失败。
inline bool iequals(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char a, char b) { return tolower(a) == tolower(b); });
}

/**
 * @brief 返回当前 system_clock 时间点，用作 UTC 时间。
 * @return 得到的时间点。
 */
inline std::chrono::system_clock::time_point getCurrentUTC() {
    return std::chrono::system_clock::now();  // 明确命名为UTC
}

/**
 * @brief 在时间点上增加指定小时数。
 * @param[in] utc_time UTC 时间值。
 * @param[in] hours 小时数。
 * @return 得到的时间点。
 */
inline std::chrono::system_clock::time_point addHours(
    const std::chrono::system_clock::time_point& utc_time, int hours) {
    return utc_time + std::chrono::hours(hours);
}

/**
 * @brief 按给定格式将时间点转换为本地时间文本。
 * @param[in] utc_time UTC 时间值。
 * @param[in] format 日志格式字符串。
 * @return 对应的文本内容。
 */
inline std::string toLocalString(const std::chrono::system_clock::time_point& utc_time,
                                 const std::string& format = "%Y-%m-%d %H:%M:%S") {
    std::time_t tt = std::chrono::system_clock::to_time_t(utc_time);
    std::tm local_tm = *std::localtime(&tt);  // 只在最后一步转换

    std::stringstream ss;
    ss << std::put_time(&local_tm, format.c_str());
    return ss.str();
}

/**
 * @brief 按给定格式将时间点转换为 UTC 文本。
 * @param[in] utc_time UTC 时间值。
 * @param[in] format 日志格式字符串。
 * @return 对应的文本内容。
 */
inline std::string toUTCString(const std::chrono::system_clock::time_point& utc_time,
                               const std::string& format = "%Y-%m-%d %H:%M:%S") {
    std::time_t tt = std::chrono::system_clock::to_time_t(utc_time);
    std::tm utc_tm = *std::gmtime(&tt);

    std::stringstream ss;
    ss << std::put_time(&utc_tm, format.c_str()) << " UTC";
    return ss.str();
}

}  // end of namespace ems
