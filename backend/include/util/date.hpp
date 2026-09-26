#pragma once
#include <ctime>
#include <regex>
#include <string>

#include "util/exception.hpp"
using namespace std;

class Date {
   private:
    int _day;
    int _month;
    int _year;

   public:
    /** @brief 用当前本地日期构造对象。 */
    Date() {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        _day = lt.tm_mday;
        _month = lt.tm_mon + 1;
        _year = lt.tm_year + 1900;
    }

    /**
     * @brief 从日、月、年构造日期；非法日期抛出异常。
     * @param[in] d 日期或时间值。
     * @param[in] m 数据映射。
     * @param[in] y 年份。
     */
    Date(int d, int m, int y) {
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

    /**
     * @brief 从本地日历时间构造日期。
     * @param[in] tm 日历时间结构。
     */
    Date(const std::tm& tm) {
        _day = tm.tm_mday;
        _month = tm.tm_mon + 1;
        _year = tm.tm_year + 1900;
    }

    /**
     * @brief 从 Unix 时间戳构造本地日期。
     * @param[in] t 待转换的枚举值。
     */
    Date(time_t t)
        : Date([](time_t t) {
              struct tm lt;
              localtime_r(&t, &lt);
              return lt;
          }(t)) {}

    /**
     * @brief 复制日期。
     * @param[in] d 日期或时间值。
     */
    Date(const Date& d) {
        _day = d._day;
        _month = d._month;
        _year = d._year;
    }

    /**
     * @brief 复制日期。
     * @param[in] d 日期或时间值。
     */
    Date(const Date&& d) {
        _day = d._day;
        _month = d._month;
        _year = d._year;
    }

    /**
     * @brief 复制赋值日期。
     * @param[in] d 日期或时间值。
     */
    Date operator=(const Date& d) {
        _day = d._day;
        _month = d._month;
        _year = d._year;
        return *this;
    }

    /**
     * @brief 复制赋值日期。
     * @param[in] d 日期或时间值。
     */
    Date operator=(const Date&& d) {
        _day = d._day;
        _month = d._month;
        _year = d._year;
        return *this;
    }

    /**
     * @brief 判断两个日期是否相同。
     * @param[in] d 日期或时间值。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const Date& d) const {
        return _year == d._year && _month == d._month && _day == d._day;
    }

    /**
     * @brief 判断两个日期是否不同。
     * @param[in] d 日期或时间值。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const Date& d) const {
        return _year != d._year || _month != d._month || _day != d._day;
    }

    /**
     * @brief 判断日期是否早于另一日期。
     * @param[in] d 日期或时间值。
     * @return true 表示左侧值按定义顺序小于右侧值。
     */
    bool operator<(const Date& d) const {
        return _year < d._year || (_year == d._year && _month < d._month) ||
               (_year == d._year && _month == d._month && _day < d._day);
    }

    /**
     * @brief 判断日期是否早于或等于另一日期。
     * @param[in] d 日期或时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator<=(const Date& d) const {
        return _year < d._year || (_year == d._year && _month < d._month) ||
               (_year == d._year && _month == d._month && _day <= d._day);
    }

    /**
     * @brief 判断日期是否晚于另一日期。
     * @param[in] d 日期或时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>(const Date& d) const {
        return _year > d._year || (_year == d._year && _month > d._month) ||
               (_year == d._year && _month == d._month && _day > d._day);
    }

    /**
     * @brief 判断日期是否晚于或等于另一日期。
     * @param[in] d 日期或时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>=(const Date& d) const {
        return _year > d._year || (_year == d._year && _month > d._month) ||
               (_year == d._year && _month == d._month && _day >= d._day);
    }

    /**
     * @brief 返回新分配的本地 tm；调用方负责释放。
     * @return 对应对象的指针。
     */
    struct tm* toLocalTime() {
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

    /**
     * @brief 将本地日期转换为 Unix 时间戳。
     * @return 得到的Unix 时间值。
     */
    time_t toTimeT() {
        struct tm lt = *toLocalTime();  // 创建一个新的 struct tm 结构体实例并复制内容
        return mktime(&lt);
    }

    /**
     * @brief 计算与另一日期相差的天数。
     * @param[in] d 日期或时间值。
     * @return 计算得到的数值。
     */
    int operator-(const Date& d) const {
        struct tm t1 = {0};
        t1.tm_year = _year - 1900;
        t1.tm_mon = _month - 1;
        t1.tm_mday = _day;
        time_t time1 = mktime(&t1);

        struct tm t2 = {0};
        t2.tm_year = d._year - 1900;
        t2.tm_mon = d._month - 1;
        t2.tm_mday = d._day;
        time_t time2 = mktime(&t2);

        int seconds = difftime(time1, time2);
        int days = seconds / (24 * 60 * 60);

        return days;
    }

    /**
     * @brief 返回增加指定天数后的日期。
     * @param[in] n 数据项数量。
     * @return 得到的日期。
     */
    Date addDays(int n) {
        time_t t = toTimeT();
        return Date(t + n * 24 * 60 * 60);
    }

    /**
     * @brief 返回增加指定月数后的日期，必要时截到月末。
     * @param[in] n 数据项数量。
     * @return 得到的日期。
     */
    Date addMonths(int n) {
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

    /**
     * @brief 返回增加指定年数后的日期，必要时截到月末。
     * @param[in] n 数据项数量。
     * @return 得到的日期。
     */
    Date addYears(int n) {
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

    /**
     * @brief 按 yyyy-mm-dd 格式输出日期。
     * @return 对应的文本内容。
     */
    std::string toString() const {
        char buf[100];
        sprintf(buf, "%04d-%02d-%02d", _year, _month, _day);
        return buf;
    }

    /**
     * @brief 按含 yyyy、mm、dd 的模板输出日期。
     * @param[in] format 日志格式字符串。
     * @return 对应的文本内容。
     */
    std::string toString(const std::string& format) const {
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

    /**
     * @brief 检查文本是否符合 yyyy-mm-dd 形状。
     * @param[in] str 输入字符串。
     * @return true 表示条件成立；false 表示不成立。
     */
    static bool isValidDate(const std::string& str) {
        std::string date = str;
        std::regex pattern(R"(^\d{4}-\d{2}-\d{2}$)");
        bool isMatch = std::regex_match(date, pattern);
        if (!isMatch) {
            return false;
        }
        return true;
    }

    /**
     * @brief 从 yyyy-mm-dd 文本解析日期；非法输入抛出异常。
     * @param[in] str 输入字符串。
     * @return 得到的日期。
     */
    static Date fromString(const std::string& str) {
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

    /** @brief 更新为当前本地日期。 */
    void update() {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        _day = lt.tm_mday;
        _month = lt.tm_mon + 1;
        _year = lt.tm_year + 1900;
    }

    /**
     * @brief 返回月份中的日。
     * @return 计算得到的数值。
     */
    int day() { return _day; }

    /**
     * @brief 返回月份编号。
     * @return 计算得到的数值。
     */
    int month() { return _month; }

    /**
     * @brief 返回年份。
     * @return 计算得到的数值。
     */
    int year() { return _year; }

    /**
     * @brief 设置月份中的日。
     * @param[in] d 日期或时间值。
     */
    void setDay(int d) { _day = d; }

    /**
     * @brief 设置月份编号。
     * @param[in] m 数据映射。
     */
    void setMonth(int m) { _month = m; }

    /**
     * @brief 设置年份。
     * @param[in] y 年份。
     */
    void setYear(int y) { _year = y; }
};
