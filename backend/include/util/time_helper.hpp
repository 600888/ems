#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

/*
获取一个time_point的指定部分，比如获取秒数：
auto ms = time_fraction<std::chrono::milliseconds>(std::chrono::system_clock::now());
*/
template <typename ToDuration>
inline ToDuration time_fraction(std::chrono::system_clock::time_point tp) {
    using std::chrono::duration_cast;
    using std::chrono::seconds;
    auto duration = tp.time_since_epoch();
    auto secs = duration_cast<seconds>(duration);
    return duration_cast<ToDuration>(duration) - duration_cast<ToDuration>(secs);
}

inline std::string time_to_string(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    return std::ctime(&t);
}

inline std::string time_to_string(time_t t) { return std::ctime(&t); }

inline std::string current_time_string() {
    return time_to_string(std::chrono::system_clock::now());
}

// 时间转换辅助函数
inline int64_t time_point_to_ms(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

inline std::chrono::system_clock::time_point ms_to_time_point(int64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

/**
 * @brief 将时间字符串转换为time_point
 *
 * @param time_str 时间字符串，格式为"YYYY-MM-DD HH:MM:SS"
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
 * @param tp time_point
 * @return 时间字符串，格式为"YYYY-MM-DD HH:MM:SS"
 */
inline std::string time_point_to_string(const std::chrono::system_clock::time_point& tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}