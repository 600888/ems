/** @file close_test.cpp
 * @brief 关闭观察器的终结顺序、幂等及迟到注册约定。
 */
#include <dlt698/session/session.hpp>
#include <dlt698/transport/memory.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;

TEST_CASE("关闭观察器在事务完成后通知且重复关闭不重复通知", "[session][close]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    auto session = std::make_shared<session::Session>(channels.first, executor);
    std::vector<int> order;
    session->set_close_handler([&](const Error& error) {
        CHECK(error.code == ErrorCode::closed);
        CHECK(session->state() == session::State::closed);
        order.push_back(2);
    });
    session->start();
    executor->run_ready();
    session->async_connect([&](auto result) {
        CHECK_FALSE(result);
        CHECK(result.error().code == ErrorCode::closed);
        order.push_back(1);
    });
    executor->run_ready();
    session->close();
    session->close();
    CHECK(order.empty());
    executor->run_ready();
    CHECK(order == std::vector<int>{1, 2});
    session->set_close_handler([&](const Error& error) {
        CHECK(error.code == ErrorCode::closed);
        order.push_back(3);
    });
    CHECK(order.size() == 2);
    executor->run_ready();
    CHECK(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("状态观察器异步报告当前状态及变化且隔离回调异常", "[session][state]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    session::Session session(channels.first, executor);
    std::vector<session::State> states;
    session.set_state_handler([&](auto value) {
        states.push_back(value);
        throw std::runtime_error("state observer failure");
    });
    CHECK(states.empty());
    session.start();
    session.close();
    session.close();
    executor->run_ready();
    CHECK(states == std::vector<session::State>{session::State::disconnected,
                                                session::State::preconnected,
                                                session::State::closed});
    session.set_state_handler({});
    executor->run_ready();
    CHECK(states.size() == 3);
}
