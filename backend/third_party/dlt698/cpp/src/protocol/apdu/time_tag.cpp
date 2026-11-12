#include <algorithm>
#include <dlt698/protocol/apdu/time_tag.hpp>

namespace dlt698::protocol::apdu {
namespace {
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }

int month_days(int y, int m) {
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[m - 1] + (m == 2 && leap(y));
}

std::int64_t stamp(const model::DateTimeS& d) {
    const auto& b = d.value;
    int y = (b[0] << 8) | b[1];
    const int m = b[2], day = b[3];
    if (y < 1 || y > 9999 || m < 1 || m > 12 || day < 1 || day > month_days(y, m) || b[4] > 23 ||
        b[5] > 59 || b[6] > 59)
        throw DecodeFailure({ErrorCode::invalid_value, 0, "TimeTag calendar"});
    // Gregorian 历法按 400 年周期计算，不调用依赖宿主时区或 time_t 宽度的 mktime。
    y -= m <= 2;
    const auto era = y / 400;
    const auto year = y - era * 400;
    const auto doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const auto days =
        static_cast<std::int64_t>(era) * 146097 + year * 365 + year / 4 - year / 100 + doy;
    return ((days * 24 + b[4]) * 60 + b[5]) * 60 + b[6];
}
}  // namespace

Result<bool> valid_time_tag(const TimeTag& tag, const model::DateTimeS& now) {
    try {
        const auto start = stamp(tag.sent_at), current = stamp(now);
        if (tag.allowed_delay.unit > 5)
            throw DecodeFailure({ErrorCode::invalid_value, 0, "TimeTag unit"});
        if (!tag.allowed_delay.interval) return true;
        std::int64_t delay;
        if (tag.allowed_delay.unit < 4) {
            constexpr std::int64_t units[] = {1, 60, 3600, 86400};
            delay = units[tag.allowed_delay.unit] * tag.allowed_delay.interval;
        } else {
            auto end = tag.sent_at;
            auto& b = end.value;
            const auto months = static_cast<int>(tag.allowed_delay.interval) *
                                (tag.allowed_delay.unit == 5 ? 12 : 1);
            const auto absolute = (((b[0] << 8) | b[1]) - 1) * 12 + (b[2] - 1) + months;
            const auto y = absolute / 12 + 1, m = absolute % 12 + 1;
            if (y > 9999)
                throw DecodeFailure({ErrorCode::invalid_value, 0, "TimeTag calendar overflow"});
            b[0] = static_cast<std::uint8_t>(y >> 8);
            b[1] = static_cast<std::uint8_t>(y);
            b[2] = static_cast<std::uint8_t>(m);
            b[3] = static_cast<std::uint8_t>(std::min<int>(b[3], month_days(y, m)));
            delay = stamp(end) - start;
        }
        const auto difference = current >= start ? current - start : start - current;
        return difference <= delay;
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::protocol::apdu
