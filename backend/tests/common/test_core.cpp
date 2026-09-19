/**
 * @file test_core.cpp
 * @brief core 基础类型测试：Result/CallContext/Identity/Time
 */
#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <unordered_set>

#include <common/core/call_context.h>
#include <common/core/identity.h>
#include <common/core/result.h>
#include <common/core/time.h>

namespace core = ems::core;

// ---------------------------------------------------------------------------
// Result / Error / WriteEffect
// ---------------------------------------------------------------------------

TEST(CoreResult, SuccessCarriesValueOnly) {
    core::Result<int> r = 42;
    ASSERT_TRUE(core::is_ok(r));
    EXPECT_EQ(core::value_of(r), 42);
}

TEST(CoreResult, ErrorCarriesDeterminism) {
    core::Result<int> r = core::make_error(core::ErrorCode::DeadlineExceeded,
                                           core::WriteEffect::Unknown, "提交响应丢失");
    ASSERT_FALSE(core::is_ok(r));
    const auto& e = core::error_of(r);
    EXPECT_EQ(e.code, core::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(e.effect, core::WriteEffect::Unknown);
}

TEST(CoreResult, OkStatus) {
    core::Status s = core::ok_status();
    EXPECT_TRUE(core::is_ok(s));
}

TEST(CoreResult, ReadFailureIsNotApplicable) {
    // 读取失败的 effect 固定为 NotApplicable
    auto e = core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable, "x");
    EXPECT_EQ(e.effect, core::WriteEffect::NotApplicable);
}

// ---------------------------------------------------------------------------
// CallContext
// ---------------------------------------------------------------------------

TEST(CoreCallContext, ExpiredAfterZeroBudget) {
    auto ctx = core::make_call_context(std::chrono::milliseconds(0));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_TRUE(core::has_expired(ctx));
    EXPECT_LT(core::remaining_budget_ms(ctx), 0);
}

TEST(CoreCallContext, NotExpiredWithBudget) {
    auto ctx = core::make_call_context(std::chrono::milliseconds(5000));
    EXPECT_FALSE(core::has_expired(ctx));
    EXPECT_GT(core::remaining_budget_ms(ctx), 4000);
}

TEST(CoreCallContext, TraceIdPreserved) {
    auto ctx = core::make_call_context(std::chrono::milliseconds(100), "trace-1");
    EXPECT_EQ(ctx.trace_id, "trace-1");
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

TEST(CoreIdentity, EqualityAndEmpty) {
    core::DeviceId a("dev-1");
    core::DeviceId b("dev-1");
    core::DeviceId c("dev-2");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_FALSE(a.empty());
    EXPECT_TRUE(static_cast<bool>(a));
    EXPECT_TRUE(core::DeviceId().empty());
}

TEST(CoreIdentity, NumericIdBecomesDecimalText) {
    // 旧数字 ID 转十进制文本，迁移不重新编号
    EXPECT_EQ(core::DeviceId::from_number(12345).value(), "12345");
    EXPECT_EQ(core::PointId::from_number(0).value(), "0");
}

TEST(CoreIdentity, StrongTypesDoNotInterconvertImplicitly) {
    // 不同强类型不能隐式比较/赋值（编译期约束，这里只验证各自构造）
    core::StationId s("1");
    core::DeviceId d("1");
    EXPECT_EQ(s.value(), d.value());  // 文本相同但类型独立
    SUCCEED();
}

TEST(CoreIdentity, HashableInUnorderedSet) {
    std::unordered_set<core::PointId> ids;
    ids.insert(core::PointId("p1"));
    ids.insert(core::PointId("p1"));
    ids.insert(core::PointId("p2"));
    EXPECT_EQ(ids.size(), 2u);
}

TEST(CoreIdentity, RevisionIsOpaque) {
    core::Revision r{"abc"};
    core::Revision same{"abc"};
    core::Revision other{"def"};
    EXPECT_EQ(r, same);
    EXPECT_NE(r, other);
    EXPECT_TRUE(core::Revision{}.empty());
}

TEST(CoreIdentity, EpochsSeparateTypes) {
    core::SourceEpoch se{7};
    core::OwnerEpoch oe{7};
    EXPECT_TRUE(se.valid());
    EXPECT_EQ(se, oe);  // 值可比较；类型语义不可互换（编译期独立）
    EXPECT_FALSE(core::SourceEpoch{}.valid());
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

TEST(CoreTime, RoundtripUtcMicros) {
    const core::UtcMicros t = 1726723200123456LL;  // 2024-09-19T05:20:00.123456Z
    auto tp = core::from_utc_micros(t);
    EXPECT_EQ(core::to_utc_micros(tp), t);
}

TEST(CoreTime, NowIsPlausible) {
    const core::UtcMicros now = core::now_utc_micros();
    // 2026-01-01 起的微秒数下限
    EXPECT_GT(now, 1767225600000000LL);
}

TEST(CoreTime, InvalidSentinel) {
    EXPECT_FALSE(core::is_valid_time(core::kInvalidUtcMicros));
    EXPECT_TRUE(core::is_valid_time(0));
    EXPECT_EQ(core::format_rfc3339_utc(core::kInvalidUtcMicros), "invalid_time");
}

TEST(CoreTime, FormatRfc3339) {
    // 2024-09-19T05:20:00.123Z
    EXPECT_EQ(core::format_rfc3339_utc(1726723200123000LL), "2024-09-19T05:20:00.123Z");
}
