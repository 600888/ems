#pragma once
#include "date.hpp"
#include "time.hpp"

class DateTime {
   private:
    Date _date;
    Time _time;

   public:
    DateTime() {
        _date = Date();
        _time = Time();
    }

    DateTime(Date d, Time t) {
        _date = d;
        _time = t;
    }

    DateTime(int d, int m, int y, int h, int min, int s) {
        _date = Date(d, m, y);
        _time = Time(h, min, s);
    }

    DateTime(time_t t) {
        if (t > 10000000000) {
            t /= 1000;
        }

        _date = Date(t);
        _time = Time(t);
    }

    DateTime(const DateTime& dt) {
        _date = dt._date;
        _time = dt._time;
    }

    DateTime(const DateTime&& dt) {
        _date = dt._date;
        _time = dt._time;
    }

    DateTime(const std::tm& tm) {
        _date = Date(tm);
        _time = Time(tm);
    }

    DateTime operator=(const DateTime& dt) {
        _date = dt._date;
        _time = dt._time;
        return *this;
    }

    DateTime operator=(const DateTime&& dt) {
        _date = dt._date;
        _time = dt._time;
        return *this;
    }

    bool operator<=(const DateTime& dt) const {
        if (_date < dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time <= dt._time;
        }
        return false;
    }

    bool operator>=(const DateTime& dt) const {
        if (_date > dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time >= dt._time;
        }
        return false;
    }

    bool operator==(const DateTime& dt) const { return _date == dt._date && _time == dt._time; }

    bool operator!=(const DateTime& dt) const { return _date != dt._date || _time != dt._time; }

    bool operator<(const DateTime& dt) const {
        if (_date < dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time < dt._time;
        }
        return false;
    }

    bool operator>(const DateTime& dt) const {
        if (_date > dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time > dt._time;
        }
        return false;
    }

    int operator-(const DateTime& dt) const {
        int seconds = (_date - dt._date) * 24 * 3600 + (_time - dt._time);
        return seconds;
    }

    Date getDate() { return _date; }
    Time getTime() { return _time; }

    std::string toString() {
        std::string res = _date.toString();
        res += " ";
        res += _time.toString();
        return res;
    }

    // 格式化输出 格式为"yyyy-mm-dd HH:MM:SS"，支持的格式有yyyy,mm,dd,HH,MM,SS
    std::string toString(const std::string& format) {
        // 用“ ”分割年月日和时分秒
        int pos = format.find(' ');
        std::string dateFmt = format.substr(0, pos);
        std::string timeFmt = format.substr(pos + 1, format.length() - pos - 1);
        std::string res = _date.toString(dateFmt);
        if (pos != std::string::npos) {
            res += " ";
            res += _time.toString(timeFmt);
        }

        return res;
    }

    // 获取初始时间，用于判空
    static DateTime origin() { return DateTime(1, 1, 1970, 0, 0, 0); }

    static DateTime fromString(const std::string& str) {
        std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");
        bool isMatch = std::regex_match(str, pattern);
        if (!isMatch) {
            throw std::runtime_error("Invalid DateTime format");
        }

        std::string date = str.substr(0, 10);
        std::string time = str.substr(11, 8);
        return DateTime(Date::fromString(date), Time::fromString(time));
    }

    // 获取时间戳
    time_t getTimestamp() {
        struct tm tm;
        tm.tm_year = _date.year() - 1900;
        tm.tm_mon = _date.month() - 1;
        tm.tm_mday = _date.day();
        tm.tm_hour = _time.hour();
        tm.tm_min = _time.minute();
        tm.tm_sec = _time.second();
        tm.tm_isdst = -1;
        return mktime(&tm);
    }

    std::tm getTm() {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        return tm;
    }

    int64_t getTimestampMs() {
        time_t t = getTimestamp();
        return t * 1000;
    }

    // 增加秒数, n为负数时为减少秒数
    DateTime addSeconds(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n);
    }

    // 增加分钟, n为负数时为减少分钟
    DateTime addMinutes(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 60);
    }

    // 增加小时, n为负数时为减少小时
    DateTime addHours(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 60 * 60);
    }

    // 增加天数, n为负数时为减少天数
    DateTime addDays(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 24 * 60 * 60);
    }

    // 增加月数, n为负数时为减少月数
    DateTime addMonths(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_mon += n;
        t = mktime(&tm);
        return DateTime(t);
    }

    // 增加年数, n为负数时为减少年数
    DateTime addYears(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_year += n;
        t = mktime(&tm);
        return DateTime(t);
    }

    // 增加秒数替换原有时间
    DateTime& addSecondsInPlace(int n) {
        time_t t = getTimestamp();
        t += n;
        *this = DateTime(t);
        return *this;
    }

    // 增加小时替换原有时间
    DateTime& addHoursInPlace(int n) {
        time_t t = getTimestamp();
        t += n * 60 * 60;
        *this = DateTime(t);
        return *this;
    }

    // 增加天数替换原有时间
    DateTime& addDaysInPlace(int n) {
        time_t t = getTimestamp();
        t += n * 24 * 60 * 60;
        *this = DateTime(t);
        return *this;
    }

    // 增加月数替换原有时间
    DateTime& addMonthsInPlace(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_mon += n;
        t = mktime(&tm);
        *this = DateTime(t);
        return *this;
    }

    // 增加年数替换原有时间
    DateTime& addYearsInPlace(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_year += n;
        t = mktime(&tm);
        *this = DateTime(t);
        return *this;
    }

    // 更新日期及时间为现在时间
    void update() {
        _date.update();
        _time.update();
    }
};