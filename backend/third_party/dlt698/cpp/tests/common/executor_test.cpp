/**
 * @file executor_test.cpp
 * @brief ManualExecutor 虚拟时钟与任务队列的单元测试。
 */
#include <chrono>
#include <dlt698/common/executor.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace std::chrono_literals;

TEST_CASE("ManualExecutor 的虚拟时钟从零开始", "[common][executor]") {
    ManualExecutor executor;
    CHECK(executor.now().time_since_epoch() == IExecutor::Clock::duration::zero());
    executor.advance(5s);
    CHECK(executor.now() == IExecutor::Clock::time_point(5s));
    executor.advance(250ms);
    CHECK(executor.now() == IExecutor::Clock::time_point(5250ms));
    CHECK(executor.now() >= executor.now());  // 单调性：now() 不随系统日期调整。
}

TEST_CASE("post 任务在run_ready 时按序执行", "[common][executor]") {
    ManualExecutor executor;
    std::vector<int> order;

    SECTION("任务按投递顺序执行") {
        for (int i = 0; i < 5; ++i) executor.post([&order, i] { order.push_back(i); });
        CHECK(order.empty());  // 投递不触发执行，须显式驱动。
        executor.run_ready();
        CHECK(order == std::vector<int>{0, 1, 2, 3, 4});
    }

    SECTION("run_ready 递归执行任务中新投递的任务") {
        // 外层任务投递内层任务，内层任务必须在本轮 run_ready 内被执行，无需再调用一次。
        int outer = 0, inner = 0;
        executor.post([&] {
            ++outer;
            executor.post([&] { ++inner; });
        });
        executor.run_ready();
        CHECK(outer == 1);
        CHECK(inner == 1);
    }

    SECTION("连续三层投递也会全部执行") {
        int count = 0;
        std::function<void()> spawn = [&] {
            if (++count < 3) {
                auto next = spawn;
                executor.post([next] { next(); });
            }
        };
        executor.post(spawn);
        executor.run_ready();
        CHECK(count == 3);
    }

    SECTION("回调异常被隔离，后续任务继续执行") {
        bool reached = false;
        executor.post([] { throw std::runtime_error("boom"); });
        executor.post([&reached] { reached = true; });
        CHECK_NOTHROW(executor.run_ready());
        CHECK(reached);
    }

    SECTION("没有任务时 run_ready 是空操作") { CHECK_NOTHROW(executor.run_ready()); }
}

TEST_CASE("schedule 任务仅在虚拟时间到期后执行", "[common][executor]") {
    ManualExecutor executor;
    int fired = 0;

    SECTION("未到期不执行，advance 到期后执行一次") {
        auto timer = executor.schedule(10s, [&fired] { ++fired; });
        REQUIRE(timer);
        executor.advance(9999ms);
        executor.run_ready();
        CHECK(fired == 0);
        executor.advance(1ms);
        executor.run_ready();
        CHECK(fired == 1);
        executor.advance(10s);
        executor.run_ready();
        CHECK(fired == 1);  // 计时器只触发一次。
    }

    SECTION("cancel 幂等且到期后不再触发") {
        auto timer = executor.schedule(5s, [&fired] { ++fired; });
        REQUIRE(timer);
        timer->cancel();
        CHECK_NOTHROW(timer->cancel());
        CHECK_NOTHROW(timer->cancel());
        executor.advance(10s);
        executor.run_ready();
        CHECK(fired == 0);
    }

    SECTION("多个计时器按到期时间先后触发") {
        std::vector<int> order;
        executor.schedule(30s, [&order] { order.push_back(3); });
        executor.schedule(10s, [&order] { order.push_back(1); });
        executor.schedule(20s, [&order] { order.push_back(2); });
        executor.advance(60s);
        executor.run_ready();
        CHECK(order == std::vector<int>{1, 2, 3});
    }

    SECTION("零延时任务在下一次 run_ready 即到期") {
        executor.schedule(0ms, [&fired] { ++fired; });
        executor.run_ready();
        CHECK(fired == 1);
    }

    SECTION("负延时被拒绝，不改变队列") {
        CHECK_THROWS_AS(executor.schedule(-1s, [] {}), std::invalid_argument);
        executor.run_ready();
        CHECK(fired == 0);
    }

    SECTION("回调异常被隔离") {
        auto timer = executor.schedule(1s, [] { throw std::runtime_error("timer boom"); });
        REQUIRE(timer);
        CHECK_NOTHROW(executor.advance(1s));
        CHECK_NOTHROW(executor.run_ready());
    }
}

TEST_CASE("advance 拒绝负向与溢出时间", "[common][executor]") {
    ManualExecutor executor;

    SECTION("负向推进被拒绝且不改变虚拟时间") {
        executor.advance(1s);
        CHECK_THROWS_AS(executor.advance(-1s), std::invalid_argument);
        CHECK(executor.now() == IExecutor::Clock::time_point(1s));
    }

    SECTION("推进到上界后无法继续前进") {
        // 上限判断用的是剩余可表示时间，虚拟时间到达上界后再推进必然越界。
        executor.advance(IExecutor::Clock::duration::max());
        CHECK(executor.now() == IExecutor::Clock::time_point::max());
        CHECK_THROWS_AS(executor.advance(1s), std::invalid_argument);
        CHECK(executor.now() == IExecutor::Clock::time_point::max());
    }

    SECTION("先推进再以超大时长推进会越界") {
        executor.advance(2s);
        const auto overflow = IExecutor::Clock::duration::max();
        CHECK_THROWS_AS(executor.advance(overflow), std::invalid_argument);
        CHECK(executor.now() == IExecutor::Clock::time_point(2s));
    }
}

TEST_CASE("is_current 反映是否正在执行任务", "[common][executor]") {
    ManualExecutor executor;
    CHECK_FALSE(executor.is_current());

    SECTION("run_ready 期间为 true") {
        bool inside = false;
        executor.post([&] { inside = executor.is_current(); });
        executor.run_ready();
        CHECK(inside);
        CHECK_FALSE(executor.is_current());
    }

    SECTION("计时器回调期间为 true") {
        bool inside = false;
        executor.schedule(1s, [&] { inside = executor.is_current(); });
        executor.advance(1s);
        executor.run_ready();
        CHECK(inside);
    }

    SECTION("嵌套调度中仍为 true") {
        bool nested = false;
        executor.post([&] { executor.post([&] { nested = executor.is_current(); }); });
        executor.run_ready();
        CHECK(nested);
    }
}

TEST_CASE("析构丢弃未驱动的任务与计时器", "[common][executor]") {
    auto executor = std::make_shared<ManualExecutor>();
    std::weak_ptr<ITimer> weak_timer;
    executor->post([] { FAIL("未驱动的任务不应执行"); });
    executor->schedule(1s, [] { FAIL("未到期的计时器不应执行"); });
    weak_timer = executor->schedule(10s, [] {});
    CHECK_FALSE(weak_timer.expired());
    executor.reset();
    CHECK(weak_timer.expired());
}