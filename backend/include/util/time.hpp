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
    // 构造函数
    Time(int h, int m, int s) {
        // 是否符合时间特性
        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
            throw std::runtime_error("Invalid time");
        }
        _hour = h;
        _minute = m;
        _second = s;
    }

    Time(time_t t) {
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    Time() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    Time(const std::tm& tm) {
        _hour = tm.tm_hour;
        _minute = tm.tm_min;
        _second = tm.tm_sec;
    }

    int operator-(const Time& t) const {
        int seconds = (_hour - t._hour) * 3600 + (_minute - t._minute) * 60 + (_second - t._second);
        return seconds;
    }

    bool operator==(const Time& t) const {
        return _hour == t._hour && _minute == t._minute && _second == t._second;
    }

    bool operator!=(const Time& t) const {
        return _hour != t._hour || _minute != t._minute || _second != t._second;
    }

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

    std::string toString() {
        char buf[100];
        sprintf(buf, "%02d:%02d:%02d", _hour, _minute, _second);
        return buf;
    }

    // 格式化输出 格式化字符串支持 hh:mm:ss
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

    static Time fromString(const std::string& str) {
        int h, m, s;
        try {
            sscanf(str.c_str(), "%d:%d:%d", &h, &m, &s);
        } catch (...) {
            throw std::runtime_error("Invalid time");
        }
        return Time(h, m, s);
    }

    // 更新为现在时间
    void update() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        _hour = now->tm_hour;
        _minute = now->tm_min;
        _second = now->tm_sec;
    }

    // 获取小时
    int hour() { return _hour; }

    // 设置小时
    void setHour(int h) { _hour = h; }

    // 获取分钟
    int minute() { return _minute; }

    // 设置分钟
    void setMinute(int m) { _minute = m; }

    // 获取秒钟
    int second() { return _second; }

    // 设置秒钟
    void setSecond(int s) { _second = s; }

    // 获取时间戳
    time_t getTimestamp() {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        now->tm_hour = _hour;
        now->tm_min = _minute;
        now->tm_sec = _second;
        return mktime(now);
    }
};
