#include <string>
#include <algorithm>
#include <sstream>
#include <time.h>
#include <chrono>
#include <iomanip>

namespace ems {

/// @brief string转小写
inline std::string to_lower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower;
}

/// @brief string转大写
inline std::string to_upper(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
    return upper;
}

/// @brief 比较两个 string 不区分大小写
inline bool iequals(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char a, char b) { return tolower(a) == tolower(b); });
}

// 阶段1：获取和存储（始终用UTC）
inline std::chrono::system_clock::time_point getCurrentUTC() {
    return std::chrono::system_clock::now(); // 明确命名为UTC
}

// 阶段2：内部处理（保持UTC）
inline std::chrono::system_clock::time_point addHours(const std::chrono::system_clock::time_point& utc_time, int hours) {
    return utc_time + std::chrono::hours(hours);
}

// 阶段3：显示/输出（根据需要转换）
inline std::string toLocalString(const std::chrono::system_clock::time_point& utc_time,
                                 const std::string& format = "%Y-%m-%d %H:%M:%S") {

    std::time_t tt = std::chrono::system_clock::to_time_t(utc_time);
    std::tm local_tm = *std::localtime(&tt); // 只在最后一步转换

    std::stringstream ss;
    ss << std::put_time(&local_tm, format.c_str());
    return ss.str();
}

inline std::string toUTCString(const std::chrono::system_clock::time_point& utc_time,
                               const std::string& format = "%Y-%m-%d %H:%M:%S") {

    std::time_t tt = std::chrono::system_clock::to_time_t(utc_time);
    std::tm utc_tm = *std::gmtime(&tt);

    std::stringstream ss;
    ss << std::put_time(&utc_tm, format.c_str()) << " UTC";
    return ss.str();
}

} // end of namespace ems
