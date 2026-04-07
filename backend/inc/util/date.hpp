#pragma once
#include <string>
#include <ctime>
#include <regex>
#include "util/exception.hpp"
using namespace std;

class Date {
private:
    int _day;
    int _month;
    int _year;

public:
    Date()
    {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        _day = lt.tm_mday;
        _month = lt.tm_mon + 1;
        _year = lt.tm_year + 1900;
    }

    Date(int d, int m, int y)
    {
        // 是否符合日期特性
        if (y < 1900 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) {
            throw std::runtime_error("Invalid date");
        }
        // 是否符合月份特性
        if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30) {
            throw std::runtime_error("Invalid date");
        }
        // 是否符合闰年特性
        if (m == 2) {
            bool isLeapYear = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
            if (isLeapYear && d > 29) {
                throw std::runtime_error("Invalid date");
            } else if (!isLeapYear && d > 28) {
                throw std::runtime_error("Invalid date");
            }
        }
        _day = d;
        _month = m;
        _year = y;
    }

    Date(const std::tm& tm)
    {
        _day = tm.tm_mday;
        _month = tm.tm_mon + 1;
        _year = tm.tm_year + 1900;
    }

    Date(time_t t)
        : Date([](time_t t) {
            struct tm lt;
            localtime_r(&t, &lt);
            return lt;
        }(t))
    {
    }

    Date(const Date& d)
    {
        _day = d._day;
        _month = d._month;
        _year = d._year;
    }

    Date(const Date&& d)
    {
        _day = d._day;
        _month = d._month;
        _year = d._year;
    }

    Date operator=(const Date& d)
    {
        _day = d._day;
        _month = d._month;
        _year = d._year;
        return *this;
    }

    Date operator=(const Date&& d)
    {
        _day = d._day;
        _month = d._month;
        _year = d._year;
        return *this;
    }

    bool operator==(const Date& d) const { return _year == d._year && _month == d._month && _day == d._day; }

    bool operator!=(const Date& d) const { return _year != d._year || _month != d._month || _day != d._day; }

    bool operator<(const Date& d) const
    {
        return _year < d._year || (_year == d._year && _month < d._month)
            || (_year == d._year && _month == d._month && _day < d._day);
    }

    bool operator<=(const Date& d) const
    {
        return _year < d._year || (_year == d._year && _month < d._month)
            || (_year == d._year && _month == d._month && _day <= d._day);
    }

    bool operator>(const Date& d) const
    {
        return _year > d._year || (_year == d._year && _month > d._month)
            || (_year == d._year && _month == d._month && _day > d._day);
    }

    bool operator>=(const Date& d) const
    {
        return _year > d._year || (_year == d._year && _month > d._month)
            || (_year == d._year && _month == d._month && _day >= d._day);
    }

    struct tm* toLocalTime()
    {
        struct tm* lt = new struct tm;
        lt->tm_sec = 0;
        lt->tm_min = 0;
        lt->tm_hour = 0;
        lt->tm_mday = _day;
        lt->tm_mon = _month - 1;
        lt->tm_year = _year - 1900;
        lt->tm_wday = 0;
        lt->tm_yday = 0;
        lt->tm_isdst = 0;
        return lt;
    }

    time_t toTimeT()
    {
        struct tm lt = *toLocalTime(); // 创建一个新的 struct tm 结构体实例并复制内容
        return mktime(&lt);
    }

    int operator-(const Date& d) const
    {
        struct tm t1 = { 0 };
        t1.tm_year = _year - 1900;
        t1.tm_mon = _month - 1;
        t1.tm_mday = _day;
        time_t time1 = mktime(&t1);

        struct tm t2 = { 0 };
        t2.tm_year = d._year - 1900;
        t2.tm_mon = d._month - 1;
        t2.tm_mday = d._day;
        time_t time2 = mktime(&t2);

        int seconds = difftime(time1, time2);
        int days = seconds / (24 * 60 * 60);

        return days;
    }

    Date addDays(int n)
    {
        time_t t = toTimeT();
        return Date(t + n * 24 * 60 * 60);
    }

    Date addMonths(int n)
    {
        int y = _year;
        int m = _month + n;
        int d = _day;

        if (n > 0) {
            // 增加月份，若次月天数小于当前天数，则取次月最后一天
            if (m > 12) {
                y += m / 12;
                m %= 12;
            }
            if (m == 2) {
                bool isLeapYear = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
                d = (isLeapYear && d > 29) ? 29 : (d > 28 ? 28 : d);
            } else if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30) {
                d = 30;
            }
        } else if (n < 0) {
            // 减少月份，若次月天数小于当前天数，则取次月最后一天
            if (m < 1) {
                y += m / 12 - 1;
                m = 12 + m % 12;
            }
            if (m == 2) {
                bool isLeapYear = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
                d = (isLeapYear && d > 29) ? 29 : (d > 28 ? 28 : d);
            } else if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30) {
                d = 30;
            }
        }

        return Date(d, m, y);
    }

    Date addYears(int n)
    {
        int y = _year + n;
        int m = _month;
        int d = _day;

        if (m == 2) {
            bool isLeapYear = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
            d = (isLeapYear && d > 29) ? 29 : (d > 28 ? 28 : d);
        } else if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30) {
            d = 30;
        }

        return Date(d, m, y);
    }

    std::string toString() const
    {
        char buf[100];
        sprintf(buf, "%04d-%02d-%02d", _year, _month, _day);
        return buf;
    }

    // 格式化输出 格式化字符串支持yyyy,mm,dd
    std::string toString(const std::string& format) const
    {
        std::string res = format;
        // 兼容大小写
        std::transform(res.begin(), res.end(), res.begin(), ::tolower);
        // 按匹配规则替换
        std::regex pattern(R"(yyyy)");
        res = std::regex_replace(res, pattern, std::to_string(_year));
        pattern = R"(mm)";
        // 单位数月份前面补0
        if (_month < 10) {
            res = std::regex_replace(res, pattern, "0" + std::to_string(_month));
        } else {
            res = std::regex_replace(res, pattern, std::to_string(_month));
        }
        pattern = R"(dd)";
        // 单位数日期前面补0
        if (_day < 10) {
            res = std::regex_replace(res, pattern, "0" + std::to_string(_day));
        } else {
            res = std::regex_replace(res, pattern, std::to_string(_day));
        }
        return res;
    }

    // 是否合法
    static bool isValidDate(const std::string& str)
    {
        std::string date = str;
        std::regex pattern(R"(^\d{4}-\d{2}-\d{2}$)");
        bool isMatch = std::regex_match(date, pattern);
        if (!isMatch) {
            return false;
        }
        return true;
    }

    static Date fromString(const std::string& str)
    {
        int d, m, y;
        if (isValidDate(str)) {
            sscanf(str.c_str(), "%04d-%02d-%02d", &y, &m, &d);
        } else {
            std::string msg = "date str = " + str + " is invalid,correct format is yyyy-mm-dd";
            // throw std::runtime_error(msg);
            THROW_RUNTIME_ERROR(msg);
        }

        return Date(d, m, y);
    }

    // 更新为当前日期
    void update()
    {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        _day = lt.tm_mday;
        _month = lt.tm_mon + 1;
        _year = lt.tm_year + 1900;
    }

    // 获取当天日期
    int day() { return _day; }

    // 获取当月月份
    int month() { return _month; }

    // 获取当年年份
    int year() { return _year; }

    // 设置当天日期
    void setDay(int d) { _day = d; }

    // 设置当月月份
    void setMonth(int m) { _month = m; }

    // 设置当年年份
    void setYear(int y) { _year = y; }
};
