/**
 * @file time_tag_test.cpp
 * @brief 时间标签日历有效性判断的单元测试。
 */
#include <dlt698/protocol/apdu/time_tag.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol::apdu;

namespace {

/// 构造带指定允许时延的时间标签，发送时间固定为 2016-05-19 08:05:00。
TimeTag tag_with_delay(std::uint8_t unit, std::uint16_t interval) {
    return TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {unit, interval}};
}

/// 断言时间标签判定结果与预期一致，并打印期望值。
void expect_valid(bool expected, const TimeTag& tag, const model::DateTimeS& now) {
    INFO("期望=" << expected);
    auto result = valid_time_tag(tag, now);
    REQUIRE(static_cast<bool>(result));
    CHECK(result.value() == expected);
}

/**
 * @brief 断言时间标签校验失败，并比对错误分类与上下文字段。
 * @param[in] tag 待校验的时间标签。
 * @param[in] now 接收时间。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 */
void require_time_tag_error(const TimeTag& tag, const model::DateTimeS& now, ErrorCode code,
                            const char* context) {
    INFO("上下文=" << context);
    auto result = valid_time_tag(tag, now);
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

}  // namespace

TEST_CASE("时间标签在允许范围内有效", "[apdu][time]") {
    const TimeTag tag = tag_with_delay(0, 30);  // 允许 30 秒

    SECTION("与发送时间相同有效") {
        expect_valid(true, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}});
    }

    SECTION("时差恰在边界内有效") {
        expect_valid(true, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x1e}});
    }

    SECTION("时差刚超出边界即失效") {
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x1f}});
    }

    SECTION("早于发送时间同样按绝对时差判定") {
        expect_valid(true, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x04, 0x1e}});
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x04, 0x1d}});
    }

    SECTION("分钟单位的边界") {
        // 允许 5 分钟，因此 08:10:00 恰好在范围内，08:10:01 已超期。
        const TimeTag minute_tag = tag_with_delay(1, 5);
        expect_valid(true, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x0a, 0x00}});
        expect_valid(false, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x0a, 0x01}});
    }

    SECTION("小时单位的边界") {
        const TimeTag hour_tag = tag_with_delay(2, 2);  // 允许 2 小时
        expect_valid(true, hour_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x0a, 0x05, 0x00}});
        expect_valid(false, hour_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x0a, 0x05, 0x01}});
    }

    SECTION("按日的单位跨零点") {
        // 允许 1 天，因此次日同一时刻仍在范围内。
        const TimeTag day_tag = tag_with_delay(3, 1);
        expect_valid(true, day_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x14, 0x08, 0x05, 0x00}});
        expect_valid(false, day_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x14, 0x08, 0x05, 0x01}});
    }

    SECTION("间隔为零时不限制时差") {
        // 间隔为零表示不设上限，但仍要求日历具体且合法。
        const TimeTag unlimited = tag_with_delay(0, 0);
        expect_valid(true, unlimited, DateTimeS{{0x07, 0xe0, 0x05, 0x19, 0x08, 0x05, 0x00}});
        expect_valid(true, unlimited, DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x10, 0x1b, 0x0b}});
    }

    SECTION("大间隔的边界仍精确到秒") {
        // 65535 秒等于 18 小时 12 分 15 秒，因此次日 02:17:15 有效、次日 02:17:16 失效。
        const TimeTag large = tag_with_delay(0, 65535);
        expect_valid(true, large, DateTimeS{{0x07, 0xe0, 0x05, 0x14, 0x02, 0x11, 0x0f}});
        expect_valid(false, large, DateTimeS{{0x07, 0xe0, 0x05, 0x14, 0x02, 0x11, 0x10}});
    }
}

TEST_CASE("已过期的时间标签失效", "[apdu][time]") {
    const TimeTag tag = tag_with_delay(0, 30);

    SECTION("隔天接收一律失效") {
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x14, 0x08, 0x05, 0x00}});
    }

    SECTION("允许时延越长容忍窗口越大") {
        // 同一接收时刻（距发送 54 分钟）在三种时延下给出不同结论。
        const model::DateTimeS now{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x3b, 0x00}};
        // 间隔为零不设上限，因此无论过多久都有效。
        expect_valid(true, tag_with_delay(0, 0), now);
        expect_valid(true, tag_with_delay(1, 60), now);
        expect_valid(true, tag_with_delay(2, 1), now);
        // 收紧到 30 秒后同一时刻即超期，可见窗口确实由间隔决定。
        expect_valid(false, tag_with_delay(0, 30), now);
    }

    SECTION("发送之前太久同样算过期") {
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x03, 0x00, 0x00}});
    }

    SECTION("前后对称的时差给出相同结论") {
        // 发送时间之前与之后等距离的时差必须对称判定。
        const TimeTag minute_tag = tag_with_delay(0, 600);  // 允许 10 分钟
        expect_valid(true, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x07, 0x37, 0x00}});
        expect_valid(true, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x0f, 0x00}});
        expect_valid(false, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x07, 0x36, 0x3b}});
        expect_valid(false, minute_tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x0f, 0x01}});
    }
}

TEST_CASE("月与年按日历推进并截到目标月末", "[apdu][time]") {
    SECTION("一个月的边界") {
        const TimeTag month = tag_with_delay(4, 1);
        // 2016-05-19 加一个月是 2016-06-19。
        expect_valid(true, month, DateTimeS{{0x07, 0xe0, 0x06, 0x13, 0x08, 0x05, 0x00}});
        expect_valid(false, month, DateTimeS{{0x07, 0xe0, 0x06, 0x13, 0x08, 0x05, 0x01}});
    }

    SECTION("目标月不存在同一日时截到月末") {
        // 2016-01-31 加一个月落在 2 月，2 月没有 31 日，因此截到 29 日。
        TimeTag january{DateTimeS{{0x07, 0xe0, 0x01, 0x1f, 0x00, 0x00, 0x00}}, {4, 1}};
        expect_valid(true, january, DateTimeS{{0x07, 0xe0, 0x02, 0x1d, 0x00, 0x00, 0x00}});
        expect_valid(false, january, DateTimeS{{0x07, 0xe0, 0x03, 0x01, 0x00, 0x00, 0x00}});
    }

    SECTION("平年二月按 28 日截断") {
        // 2015-02 没有 29 日，因此 1 月 31 日加一个月截到 28 日。
        TimeTag january{DateTimeS{{0x07, 0xdf, 0x01, 0x1f, 0x00, 0x00, 0x00}}, {4, 1}};
        expect_valid(true, january, DateTimeS{{0x07, 0xdf, 0x02, 0x1c, 0x00, 0x00, 0x00}});
        expect_valid(false, january, DateTimeS{{0x07, 0xdf, 0x03, 0x01, 0x00, 0x00, 0x00}});
    }

    SECTION("世纪年 2100 不是闰年") {
        // 2100 能被 100 整除但不能被 400 整除，因此二月只有 28 天。
        TimeTag january{DateTimeS{{0x08, 0x34, 0x01, 0x1f, 0x00, 0x00, 0x00}}, {4, 1}};
        expect_valid(true, january, DateTimeS{{0x08, 0x34, 0x02, 0x1c, 0x00, 0x00, 0x00}});
    }

    SECTION("单位 5 表示按年增加") {
        const TimeTag year = tag_with_delay(5, 1);
        expect_valid(true, year, DateTimeS{{0x07, 0xe1, 0x05, 0x13, 0x08, 0x05, 0x00}});
        expect_valid(false, year, DateTimeS{{0x07, 0xe1, 0x05, 0x13, 0x08, 0x05, 0x01}});
    }

    SECTION("单位 5 的间隔按十二个月累加") {
        // 两年相当于 24 个月，因此 2018-05-19 恰好在边界内。
        const TimeTag two_years = tag_with_delay(5, 2);
        expect_valid(true, two_years, DateTimeS{{0x07, 0xe2, 0x05, 0x13, 0x08, 0x05, 0x00}});
        expect_valid(false, two_years, DateTimeS{{0x07, 0xe2, 0x05, 0x13, 0x08, 0x05, 0x01}});
    }

    SECTION("多个自然月的边界") {
        const TimeTag six_months = tag_with_delay(4, 6);
        expect_valid(true, six_months, DateTimeS{{0x07, 0xe0, 0x0b, 0x13, 0x08, 0x05, 0x00}});
        expect_valid(false, six_months, DateTimeS{{0x07, 0xe0, 0x0b, 0x13, 0x08, 0x05, 0x01}});
    }

    SECTION("闰日加一个月仍按目标月推进") {
        // 2016-02-29 加一个月是 3 月 29 日，不做月末截断。
        TimeTag leap{DateTimeS{{0x07, 0xe0, 0x02, 0x1d, 0x08, 0x05, 0x00}}, {4, 1}};
        expect_valid(true, leap, DateTimeS{{0x07, 0xe0, 0x03, 0x1d, 0x08, 0x05, 0x00}});
    }
}

TEST_CASE("FF 未指定标记被拒绝", "[apdu][time]") {
    const model::DateTimeS now{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};

    SECTION("发送时间全 FF 表示未指定，不是合法时标") {
        TimeTag unspecified{DateTimeS{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}, {0, 0}};
        // 未指定的时间无法定位时点，因此即便零间隔也不接受。
        require_time_tag_error(unspecified, now, ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("年字节为 FF 同样被拒绝") {
        TimeTag unspecified{DateTimeS{{0xff, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 30}};
        require_time_tag_error(unspecified, now, ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("年字段由两字节组成，低字节不参与未指定判定") {
        // 0x07FF 是合法年份 2047，因此低字节为 FF 不等于未指定。
        TimeTag year_2047{DateTimeS{{0x07, 0xff, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 0}};
        expect_valid(true, year_2047, year_2047.sent_at);
        // 高字节为 FF 则构成0xFFFF，超出 9999 上限，必须拒绝。
        TimeTag unspecified{DateTimeS{{0xff, 0xff, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 30}};
        require_time_tag_error(unspecified, now, ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("月、日、时、分、秒的 FF 都非法") {
        for (std::size_t index :
             {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
            INFO("FF 位置=" << index);
            model::DateTimeS sent = now;
            sent.value[index] = 0xff;
            TimeTag unspecified{sent, {0, 0}};
            require_time_tag_error(unspecified, now, ErrorCode::invalid_value, "TimeTag calendar");
        }
    }

    SECTION("接收时间出现 FF 同样被拒绝") {
        const TimeTag tag = tag_with_delay(0, 30);
        for (std::size_t index :
             {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
            INFO("FF 位置=" << index);
            model::DateTimeS bad = now;
            bad.value[index] = 0xff;
            require_time_tag_error(tag, bad, ErrorCode::invalid_value, "TimeTag calendar");
        }
    }
}

TEST_CASE("非法日历字段被精确拒绝", "[apdu][time]") {
    const model::DateTimeS now{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};

    SECTION("年份下界为一") {
        TimeTag zero_year{DateTimeS{{0x00, 0x00, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 0}};
        require_time_tag_error(zero_year, now, ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("年份上界为 9999") {
        TimeTag maximum{DateTimeS{{0x27, 0x0f, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 0}};
        expect_valid(true, maximum, DateTimeS{{0x27, 0x0f, 0x05, 0x13, 0x08, 0x05, 0x00}});
    }

    SECTION("月为零或超过 12") {
        for (std::uint8_t month : {0, 13, 0xff}) {
            INFO("月=" << +month);
            TimeTag bad{DateTimeS{{0x07, 0xe0, month, 0x13, 0x08, 0x05, 0x00}}, {0, 0}};
            require_time_tag_error(bad, now, ErrorCode::invalid_value, "TimeTag calendar");
        }
    }

    SECTION("日为零或超过当月天数") {
        TimeTag zero_day{DateTimeS{{0x07, 0xe0, 0x05, 0x00, 0x08, 0x05, 0x00}}, {0, 0}};
        require_time_tag_error(zero_day, now, ErrorCode::invalid_value, "TimeTag calendar");

        // 2016 年是闰年，2 月 29 日合法；2015 与 2100 不是。
        TimeTag leap_day{DateTimeS{{0x07, 0xe0, 0x02, 0x1d, 0x08, 0x05, 0x00}}, {0, 0}};
        expect_valid(true, leap_day, leap_day.sent_at);
        TimeTag not_leap{DateTimeS{{0x07, 0xdf, 0x02, 0x1d, 0x08, 0x05, 0x00}}, {0, 0}};
        require_time_tag_error(not_leap, now, ErrorCode::invalid_value, "TimeTag calendar");
        TimeTag century{DateTimeS{{0x08, 0x34, 0x02, 0x1d, 0x00, 0x00, 0x00}}, {3, 1}};
        require_time_tag_error(century, DateTimeS{{0x08, 0x34, 0x03, 0x01, 0x00, 0x00, 0x00}},
                               ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("时、分、秒越界") {
        TimeTag hour{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x18, 0x05, 0x00}}, {0, 0}};
        require_time_tag_error(hour, now, ErrorCode::invalid_value, "TimeTag calendar");
        TimeTag minute{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x3c, 0x00}}, {0, 0}};
        require_time_tag_error(minute, now, ErrorCode::invalid_value, "TimeTag calendar");
        TimeTag second{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x3c}}, {0, 0}};
        require_time_tag_error(second, now, ErrorCode::invalid_value, "TimeTag calendar");
    }

    SECTION("接收时间的非法字段同样被拒绝") {
        const TimeTag tag = tag_with_delay(0, 30);
        for (std::size_t index :
             {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}, std::size_t{6}}) {
            INFO("FF 位置=" << index);
            model::DateTimeS bad = now;
            bad.value[index] = 0xff;
            require_time_tag_error(tag, bad, ErrorCode::invalid_value, "TimeTag calendar");
        }
    }
}

TEST_CASE("时间单位与日历溢出被拒绝", "[apdu][time]") {
    const model::DateTimeS now{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};

    SECTION("单位只能是 0 至 5") {
        for (std::uint8_t unit = 0; unit <= 5; ++unit) {
            INFO("单位=" << +unit);
            expect_valid(true, tag_with_delay(unit, 1), now);
        }
        for (std::uint8_t unit : {6, 7, 0x80, 0xff}) {
            INFO("非法单位=" << +unit);
            require_time_tag_error(tag_with_delay(unit, 1), now, ErrorCode::invalid_value,
                                   "TimeTag unit");
        }
    }

    SECTION("年月推进超过 9999 年报溢出") {
        // 9999-12-31 再加一个月或一年都会越界，因此报错而不是回绕。
        const model::DateTimeS maximum{{0x27, 0x0f, 0x0c, 0x1f, 0x00, 0x00, 0x00}};
        require_time_tag_error(TimeTag{maximum, {4, 1}}, maximum, ErrorCode::invalid_value,
                               "TimeTag calendar overflow");
        require_time_tag_error(TimeTag{maximum, {5, 1}}, maximum, ErrorCode::invalid_value,
                               "TimeTag calendar overflow");
    }

    SECTION("恰好落在 9999 年内不报溢出") {
        const model::DateTimeS maximum{{0x27, 0x0f, 0x0c, 0x1f, 0x00, 0x00, 0x00}};
        // 零间隔不触发日历推进，因此不涉及溢出。
        expect_valid(true, tag_with_delay(0, 0), maximum);
        // 9998-12-31 加一年恰好到 9999-12-31。
        TimeTag almost{DateTimeS{{0x27, 0x0e, 0x0c, 0x1f, 0x00, 0x00, 0x00}}, {5, 1}};
        expect_valid(true, almost, DateTimeS{{0x27, 0x0f, 0x0c, 0x1f, 0x00, 0x00, 0x00}});
    }

    SECTION("单位越界优先于溢出被报告") {
        // 单位非法时先报单位错误，避免用非法单位推算日历。
        const model::DateTimeS maximum{{0x27, 0x0f, 0x0c, 0x1f, 0x00, 0x00, 0x00}};
        require_time_tag_error(TimeTag{maximum, {6, 1}}, maximum, ErrorCode::invalid_value,
                               "TimeTag unit");
    }

    SECTION("零间隔时非法单位仍被拒绝") {
        // 即使间隔为零使时差不参与判定，单位字段本身仍须合法。
        require_time_tag_error(tag_with_delay(6, 0), now, ErrorCode::invalid_value, "TimeTag unit");
    }
}

TEST_CASE("时间标签判定与宿主时区无关", "[apdu][time]") {
    SECTION("跨年与跨世纪边界") {
        // 1999-12-31 23:59:59 加 1 秒落在 2000 年。
        TimeTag new_year{DateTimeS{{0x07, 0xe7, 0x0c, 0x1f, 0x17, 0x3b, 0x3b}}, {0, 1}};
        expect_valid(true, new_year, DateTimeS{{0x07, 0xe8, 0x01, 0x01, 0x00, 0x00, 0x00}});
        expect_valid(false, new_year, DateTimeS{{0x07, 0xe8, 0x01, 0x01, 0x00, 0x00, 0x01}});
    }

    SECTION("四百年闰年规则") {
        // 2000 能被 400 整除，2 月 29 日存在，按日加一天落在 3 月 1 日。
        TimeTag y2000{DateTimeS{{0x07, 0xd0, 0x02, 0x1d, 0x00, 0x00, 0x00}}, {3, 1}};
        expect_valid(true, y2000, DateTimeS{{0x07, 0xd0, 0x03, 0x01, 0x00, 0x00, 0x00}});
    }

    SECTION("同一时间基准下的对称判定") {
        // 允许 60 秒，因此前后各 60 秒有效，超出一秒即失效。
        const TimeTag tag = tag_with_delay(0, 60);
        expect_valid(true, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x04, 0x00}});
        expect_valid(true, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x00}});
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x03, 0x3b}});
        expect_valid(false, tag, DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x01}});
    }
}