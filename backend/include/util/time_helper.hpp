#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

/**
 * @brief 返回时间点在整秒之外的指定精度余量。
 * @tparam ToDuration 期望的持续时间精度。
 * @param[in] tp 时间点。
 * @return 得到的时间长度。
 */
template <typename ToDuration>
inline ToDuration time_fraction(std::chrono::system_clock::time_point tp) {
    using std::chrono::duration_cast;
    using std::chrono::seconds;
    auto duration = tp.time_since_epoch();
    auto secs = duration_cast<seconds>(duration);
    return duration_cast<ToDuration>(duration) - duration_cast<ToDuration>(secs);
}

/**
 * @brief 使用 ctime 将时间点转为本地时间文本。
 * @param[in] tp 时间点。
 * @return 对应的文本内容。
 */
inline std::string time_to_string(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    return std::ctime(&t);
}

/**
 * @brief 使用 ctime 将 Unix 秒时间戳转为本地时间文本。
 * @param[in] t 待转换的枚举值。
 * @return 对应的文本内容。
 */
inline std::string time_to_string(time_t t) { return std::ctime(&t); }

/**
 * @brief 返回当前本地时间的 ctime 文本。
 * @return 对应的文本内容。
 */
inline std::string current_time_string() {
    return time_to_string(std::chrono::system_clock::now());
}

/**
 * @brief 将系统时间点转换为 Unix 毫秒。
 * @param[in] tp 时间点。
 * @return 计算得到的数值。
 */
inline int64_t time_point_to_ms(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

/**
 * @brief 从 Unix 毫秒还原系统时间点。
 * @param[in] ms 时间长度，单位为毫秒。
 * @return 得到的时间点。
 */
inline std::chrono::system_clock::time_point ms_to_time_point(int64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

/**
 * @brief 将时间字符串转换为time_point
 *
 * @param[in] time_str 时间字符串，格式为"YYYY-MM-DD HH:MM:SS"
 * @return 对应的time_point
 */
inline std::chrono::system_clock::time_point string_to_time_point(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

/**
 * @brief 将time_point转换为时间字符串
 *
 * @param[in] tp time_point
 * @return 时间字符串，格式为"YYYY-MM-DD HH:MM:SS"
 */
inline std::string time_point_to_string(const std::chrono::system_clock::time_point& tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
