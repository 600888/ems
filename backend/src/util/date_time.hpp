#pragma once
#include "date.hpp"
#include "time.hpp"

class DateTime {
   private:
    Date _date;
    Time _time;

   public:
    /** @brief 用当前本地日期和时间构造对象。 */
    DateTime() {
        _date = Date();
        _time = Time();
    }

    /**
     * @brief 从日期和时间对象构造。
     * @param[in] d 日期或时间值。
     * @param[in] t 待转换的枚举值。
     */
    DateTime(Date d, Time t) {
        _date = d;
        _time = t;
    }

    /**
     * @brief 从日、月、年、时、分、秒构造。
     * @param[in] d 日期或时间值。
     * @param[in] m 数据映射。
     * @param[in] y 年份。
     * @param[in] h 小时数。
     * @param[in] min 分钟数。
     * @param[in] s 待解析的文本。
     */
    DateTime(int d, int m, int y, int h, int min, int s) {
        _date = Date(d, m, y);
        _time = Time(h, min, s);
    }

    /**
     * @brief 从秒或毫秒时间戳构造本地日期时间。
     * @param[in] t 待转换的枚举值。
     */
    DateTime(time_t t) {
        if (t > 10000000000) {
            t /= 1000;
        }

        _date = Date(t);
        _time = Time(t);
    }

    /**
     * @brief 复制日期时间。
     * @param[in] dt 日期时间值。
     */
    DateTime(const DateTime& dt) {
        _date = dt._date;
        _time = dt._time;
    }

    /**
     * @brief 复制日期时间。
     * @param[in] dt 日期时间值。
     */
    DateTime(const DateTime&& dt) {
        _date = dt._date;
        _time = dt._time;
    }

    /**
     * @brief 从本地日历时间构造。
     * @param[in] tm 日历时间结构。
     */
    DateTime(const std::tm& tm) {
        _date = Date(tm);
        _time = Time(tm);
    }

    /**
     * @brief 复制赋值日期时间。
     * @param[in] dt 日期时间值。
     */
    DateTime operator=(const DateTime& dt) {
        _date = dt._date;
        _time = dt._time;
        return *this;
    }

    /**
     * @brief 复制赋值日期时间。
     * @param[in] dt 日期时间值。
     */
    DateTime operator=(const DateTime&& dt) {
        _date = dt._date;
        _time = dt._time;
        return *this;
    }

    /**
     * @brief 判断是否早于或等于另一日期时间。
     * @param[in] dt 日期时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator<=(const DateTime& dt) const {
        if (_date < dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time <= dt._time;
        }
        return false;
    }

    /**
     * @brief 判断是否晚于或等于另一日期时间。
     * @param[in] dt 日期时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>=(const DateTime& dt) const {
        if (_date > dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time >= dt._time;
        }
        return false;
    }

    /**
     * @brief 判断两个日期时间是否相同。
     * @param[in] dt 日期时间值。
     * @return true 表示两者相等；false 表示不相等。
     */
    bool operator==(const DateTime& dt) const { return _date == dt._date && _time == dt._time; }

    /**
     * @brief 判断两个日期时间是否不同。
     * @param[in] dt 日期时间值。
     * @return true 表示两者不相等；false 表示相等。
     */
    bool operator!=(const DateTime& dt) const { return _date != dt._date || _time != dt._time; }

    /**
     * @brief 判断是否早于另一日期时间。
     * @param[in] dt 日期时间值。
     * @return true 表示左侧值按定义顺序小于右侧值。
     */
    bool operator<(const DateTime& dt) const {
        if (_date < dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time < dt._time;
        }
        return false;
    }

    /**
     * @brief 判断是否晚于另一日期时间。
     * @param[in] dt 日期时间值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator>(const DateTime& dt) const {
        if (_date > dt._date) {
            return true;
        } else if (_date == dt._date) {
            return _time > dt._time;
        }
        return false;
    }

    /**
     * @brief 计算与另一日期时间相差的秒数。
     * @param[in] dt 日期时间值。
     * @return 计算得到的数值。
     */
    int operator-(const DateTime& dt) const {
        int seconds = (_date - dt._date) * 24 * 3600 + (_time - dt._time);
        return seconds;
    }

    /**
     * @brief 返回日期部分。
     * @return 得到的日期。
     */
    Date getDate() { return _date; }

    /**
     * @brief 返回时间部分。
     * @return 得到的时间。
     */
    Time getTime() { return _time; }

    /**
     * @brief 按 yyyy-mm-dd HH:MM:SS 输出。
     * @return 对应的文本内容。
     */
    std::string toString() {
        std::string res = _date.toString();
        res += " ";
        res += _time.toString();
        return res;
    }

    /**
     * @brief 按指定日期时间模板输出。
     * @param[in] format 日志格式字符串。
     * @return 对应的文本内容。
     */
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

    /**
     * @brief 返回用于空值判定的初始时间。
     * @return 得到的日期时间。
     */
    static DateTime origin() { return DateTime(1, 1, 1970, 0, 0, 0); }

    /**
     * @brief 解析 yyyy-mm-dd HH:MM:SS；非法输入抛出异常。
     * @param[in] str 输入字符串。
     * @return 得到的日期时间。
     */
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

    /**
     * @brief 转换为本地 Unix 秒时间戳。
     * @return 得到的Unix 时间值。
     */
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

    /**
     * @brief 返回本地日历时间结构。
     * @return 得到的日历时间结构。
     */
    std::tm getTm() {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        return tm;
    }

    /**
     * @brief 转换为 Unix 毫秒时间戳。
     * @return 查询得到的数值。
     */
    int64_t getTimestampMs() {
        time_t t = getTimestamp();
        return t * 1000;
    }

    /**
     * @brief 返回增加指定秒数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addSeconds(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n);
    }

    /**
     * @brief 返回增加指定分钟数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addMinutes(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 60);
    }

    /**
     * @brief 返回增加指定小时数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addHours(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 60 * 60);
    }

    /**
     * @brief 返回增加指定天数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addDays(int n) {
        time_t t = getTimestamp();
        return DateTime(t + n * 24 * 60 * 60);
    }

    /**
     * @brief 返回增加指定月数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addMonths(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_mon += n;
        t = mktime(&tm);
        return DateTime(t);
    }

    /**
     * @brief 返回增加指定年数后的副本。
     * @param[in] n 数据项数量。
     * @return 得到的日期时间。
     */
    DateTime addYears(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_year += n;
        t = mktime(&tm);
        return DateTime(t);
    }

    /**
     * @brief 在当前对象上增加指定秒数。
     * @param[in] n 数据项数量。
     * @return 对应对象的引用。
     */
    DateTime& addSecondsInPlace(int n) {
        time_t t = getTimestamp();
        t += n;
        *this = DateTime(t);
        return *this;
    }

    /**
     * @brief 在当前对象上增加指定小时数。
     * @param[in] n 数据项数量。
     * @return 对应对象的引用。
     */
    DateTime& addHoursInPlace(int n) {
        time_t t = getTimestamp();
        t += n * 60 * 60;
        *this = DateTime(t);
        return *this;
    }

    /**
     * @brief 在当前对象上增加指定天数。
     * @param[in] n 数据项数量。
     * @return 对应对象的引用。
     */
    DateTime& addDaysInPlace(int n) {
        time_t t = getTimestamp();
        t += n * 24 * 60 * 60;
        *this = DateTime(t);
        return *this;
    }

    /**
     * @brief 在当前对象上增加指定月数。
     * @param[in] n 数据项数量。
     * @return 对应对象的引用。
     */
    DateTime& addMonthsInPlace(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_mon += n;
        t = mktime(&tm);
        *this = DateTime(t);
        return *this;
    }

    /**
     * @brief 在当前对象上增加指定年数。
     * @param[in] n 数据项数量。
     * @return 对应对象的引用。
     */
    DateTime& addYearsInPlace(int n) {
        time_t t = getTimestamp();
        struct tm tm;
        localtime_r(&t, &tm);
        tm.tm_year += n;
        t = mktime(&tm);
        *this = DateTime(t);
        return *this;
    }

    /** @brief 更新为当前本地日期和时间。 */
    void update() {
        _date.update();
        _time.update();
    }
};