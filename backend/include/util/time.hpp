#pragma once
#include <ctime>
#include <regex>
#include <string>

class Time {
   private:
    int _hour;
    int _minute;
    int _second;

   public:
    /**
     * @brief 从时、分、秒构造时间；非法值抛出异常。
     * @param[in] h 小时数。
     * @param[in] m 数据映射。
     * @param[in] s 待解析的文本。
     */
    Time(int h, int m, int s) {
        // 是否符合时间特性
        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
            throw std::runtime_error("Invalid time");
        }
        _hour = h;
        _minute = m;
        _second = s;
    }

    /**
     * @brief 从 Unix 时间戳构造本地时间。
     * @param[in] t 待转换的枚举值。
     */
    Time(time_t t) {
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    /** @brief 用当前本地时间构造对象。 */
    Time() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    /**
     * @brief 从本地日历时间构造对象。
     * @param[in] tm 日历时间结构。
     */
    Time(const std::tm& tm) {
        _hour = tm.tm_hour;
        _minute = tm.tm_min;
        _second = tm.tm_sec;
    }

    /**
     * @brief 计算与另一时间相差的秒数。
     * @param[in] t 待转换的枚举值。
     * @return 计算得到的数值。
     */
    int operator-(const Time& t) const {
        int seconds = (_hour - t._hour) * 3600 + (_minute - t._minute) * 60 + (_second - t._second);
        return seconds;
    }

    /**
     * @brief 判断两个时间是否相同。
     * @param[in] t 待转换的枚举值。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const Time& t) const {
        return _hour == t._hour && _minute == t._minute && _second == t._second;
    }

    /**
     * @brief 判断两个时间是否不同。
     * @param[in] t 待转换的枚举值。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const Time& t) const {
        return _hour != t._hour || _minute != t._minute || _second != t._second;
    }

    /**
     * @brief 判断时间是否早于另一时间。
     * @param[in] t 待转换的枚举值。
     * @return true 表示左侧值按定义顺序小于右侧值。
     */
    bool operator<(const Time& t) const {
        if (_hour < t._hour) {
            return true;
        } else if (_hour == t._hour) {
            if (_minute < t._minute) {
                return true;
            } else if (_minute == t._minute) {
                return _second < t._second;
            }
        }
        return false;
    }

    /**
     * @brief 判断时间是否晚于另一时间。
     * @param[in] t 待转换的枚举值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>(const Time& t) const {
        if (_hour > t._hour) {
            return true;
        } else if (_hour == t._hour) {
            if (_minute > t._minute) {
                return true;
            } else if (_minute == t._minute) {
                return _second > t._second;
            }
        }
        return false;
    }

    /**
     * @brief 判断时间是否早于或等于另一时间。
     * @param[in] t 待转换的枚举值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator<=(const Time& t) const {
        if (_hour < t._hour) {
            return true;
        } else if (_hour == t._hour) {
            if (_minute < t._minute) {
                return true;
            } else if (_minute == t._minute) {
                return _second <= t._second;
            }
        }
        return false;
    }

    /**
     * @brief 判断时间是否晚于或等于另一时间。
     * @param[in] t 待转换的枚举值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>=(const Time& t) const {
        if (_hour > t._hour) {
            return true;
        } else if (_hour == t._hour) {
            if (_minute > t._minute) {
                return true;
            } else if (_minute == t._minute) {
                return _second >= t._second;
            }
        }
        return false;
    }

    /**
     * @brief 按 HH:MM:SS 格式输出时间。
     * @return 对应的文本内容。
     */
    std::string toString() {
        char buf[100];
        sprintf(buf, "%02d:%02d:%02d", _hour, _minute, _second);
        return buf;
    }

    /**
     * @brief 按含 HH、MM、SS 的模板输出时间。
     * @param[in] format 日志格式字符串。
     * @return 对应的文本内容。
     */
    std::string toString(const std::string& format) {
        std::string res = format;
        // 兼容大小写
        std::transform(res.begin(), res.end(), res.begin(), ::tolower);
        // 按匹配规则替换
        std::regex pattern(R"(hh)");
        // 单位数小时前面补0
        if (_hour < 10) {
            res = std::regex_replace(res, pattern, "0" + std::to_string(_hour));
        } else {
            res = std::regex_replace(res, pattern, std::to_string(_hour));
        }
        pattern = R"(mm)";
        // 单位数分钟前面补0
        if (_minute < 10) {
            res = std::regex_replace(res, pattern, "0" + std::to_string(_minute));
        } else {
            res = std::regex_replace(res, pattern, std::to_string(_minute));
        }
        pattern = R"(ss)";
        // 单位数秒钟前面补0
        if (_second < 10) {
            res = std::regex_replace(res, pattern, "0" + std::to_string(_second));
        } else {
            res = std::regex_replace(res, pattern, std::to_string(_second));
        }
        return res;
    }

    /**
     * @brief 从 HH:MM:SS 文本解析时间；非法输入抛出异常。
     * @param[in] str 输入字符串。
     * @return 得到的时间。
     */
    static Time fromString(const std::string& str) {
        int h, m, s;
        try {
            sscanf(str.c_str(), "%d:%d:%d", &h, &m, &s);
        } catch (...) {
            throw std::runtime_error("Invalid time");
        }
        return Time(h, m, s);
    }

    /** @brief 更新为当前本地时间。 */
    void update() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    /**
     * @brief 返回小时。
     * @return 计算得到的数值。
     */
    int hour() { return _hour; }

    /**
     * @brief 设置小时。
     * @param[in] h 小时数。
     */
    void setHour(int h) { _hour = h; }

    /**
     * @brief 返回分钟。
     * @return 计算得到的数值。
     */
    int minute() { return _minute; }

    /**
     * @brief 设置分钟。
     * @param[in] m 数据映射。
     */
    void setMinute(int m) { _minute = m; }

    /**
     * @brief 返回秒数。
     * @return 计算得到的数值。
     */
    int second() { return _second; }

    /**
     * @brief 设置秒数。
     * @param[in] s 待解析的文本。
     */
    void setSecond(int s) { _second = s; }

    /**
     * @brief 将时间字段应用于当前本地日期并返回时间戳。
     * @return 得到的Unix 时间值。
     */
    time_t getTimestamp() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        now->tm_hour = _hour;
        now->tm_min = _minute;
        now->tm_sec = _second;
        return mktime(now);
    }
};
