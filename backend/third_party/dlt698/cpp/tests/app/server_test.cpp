/** @file server_test.cpp
 * @brief 托管服务器真实 TCP 回环、共享数据、失败回收和回调析构测试。
 */
#include <atomic>
#include <dlt698/app.hpp>
#include <dlt698/service/sync.hpp>
#include <future>
#include <thread>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::app;
using namespace std::chrono_literals;

TEST_CASE("IoRuntime finish 排空取消完成回调而非提前停止", "[app][runtime]") {
    auto runtime = std::make_shared<transport::IoRuntime>();
    auto bound = transport::TcpListener::listen(runtime, "127.0.0.1", 0);
    REQUIRE(bound);
    auto listener = bound.value();
    int calls = 0;
    listener->async_accept([&](auto result) {
        CHECK_FALSE(result);
        ++calls;
    });
    auto executor = runtime->executor();
    bool timer_fired = false;
    auto timer = executor->schedule(1h, [&] { timer_fired = true; });
    timer->cancel();
    listener->close();
    runtime->finish();
    runtime->finish();
    runtime->run();
    CHECK(calls == 1);
    CHECK_FALSE(timer_fired);
}

namespace {
/** @brief 在有界时间内驱动测试客户机，服务端由自己的线程驱动。 */
void until(const std::shared_ptr<transport::IoRuntime>& runtime,
           const std::function<bool()>& predicate) {
    const auto end = IExecutor::Clock::now() + 3s;
    while (!predicate() && IExecutor::Clock::now() < end) runtime->run_for(2ms);
    REQUIRE(predicate());
}

/** @brief 测试专用分层客户机，验证新服务器与已有公开协议接口兼容。 */
struct Peer {
    std::shared_ptr<transport::IoRuntime> runtime = std::make_shared<transport::IoRuntime>();
    std::shared_ptr<transport::TcpChannel> channel;
    std::shared_ptr<session::Session> session;
    std::unique_ptr<service::SyncClientService> client;

    explicit Peer(std::uint16_t port, bool remote = false, bool preset = false) {
        auto completion = std::make_shared<std::optional<Result<void>>>();
        channel = transport::TcpChannel::connect(
            runtime, "127.0.0.1", port, [completion](auto r) { *completion = std::move(r); });
        until(runtime, [completion] { return completion->has_value(); });
        REQUIRE(**completion);
        session::SessionOptions options;
        options.require_login = remote;
        options.preset_association = preset;
        options.request_timeout = 500ms;
        session = std::make_shared<session::Session>(channel, runtime->executor(), options);
        session->start();
        until(runtime, [this, preset] {
            return session->state() ==
                   (preset ? session::State::associated : session::State::preconnected);
        });
        client = std::make_unique<service::SyncClientService>(
            session, [runtime = runtime](auto duration) { runtime->run_for(duration); });
        if (!preset) {
            auto connected = client->connect();
            REQUIRE(connected);
            REQUIRE(connected.value().result == 0);
        }
    }

    ~Peer() {
        session->close();
        runtime->finish();
        runtime->run();
    }

    std::uint16_t frequency() {
        auto result = client->get({0x200F, 2, 0});
        REQUIRE(result);
        REQUIRE(std::holds_alternative<model::Data>(result.value()));
        return std::get<model::Data>(result.value()).as<model::UInt16>().value;
    }
};
}  // namespace

TEST_CASE("托管 TCP 运行中发布数据并共享于多个连接", "[app][tcp]") {
    Server server;
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    CHECK(server.state() == ServerState::running);
    CHECK(server.local_port() != 0);
    CHECK_FALSE(server.start_tcp("127.0.0.1", 0));
    Peer first(server.local_port());
    auto absent = first.client->get({0x200F, 2, 0});
    REQUIRE(absent);
    CHECK(std::get<std::uint8_t>(absent.value()) == 4);
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
    Peer second(server.local_port());
    CHECK(first.frequency() == 5000);
    CHECK(second.frequency() == 5000);
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{4998}));
    CHECK(first.frequency() == 4998);
    CHECK(second.frequency() == 4998);
    auto rejected = first.client->set({0x200F, 2, 0}, model::UInt16{1});
    REQUIRE(rejected);
    CHECK(rejected.value() == 3);
    CHECK(server.connections() == 2);
    REQUIRE(server.stop());
    CHECK(server.state() == ServerState::stopped);
    CHECK(server.connections() == 0);
    CHECK(server.local_port() == 0);
    REQUIRE(server.stop());
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    Peer after_restart(server.local_port());
    CHECK(after_restart.frequency() == 4998);
}

TEST_CASE("默认 TCP 入口自动发起远程 LINK 登录", "[app][tcp][login]") {
    Server server;
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
    REQUIRE(server.start_tcp("127.0.0.1", 0));
    Peer peer(server.local_port(), true);
    CHECK(peer.frequency() == 5000);
    REQUIRE(peer.client->release());
    REQUIRE(server.stop());
}

TEST_CASE("多个服务器共享设备且停止不清空数据", "[app][tcp]") {
    auto device = std::make_shared<service::Device>();
    Server first(device), second(device);
    REQUIRE(first.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    REQUIRE(second.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    REQUIRE(first.set({0x200F, 2, 0}, model::UInt16{4999}));
    Peer a(first.local_port()), b(second.local_port());
    CHECK(a.frequency() == 4999);
    CHECK(b.frequency() == 4999);
    REQUIRE(first.stop());
    REQUIRE(second.set({0x200F, 2, 0}, model::UInt16{5001}));
    CHECK(b.frequency() == 5001);
}

TEST_CASE("TCP 连接超限后关闭释放名额且数据跨断线保留", "[app][tcp][limits]") {
    auto refused = std::make_shared<std::promise<void>>();
    auto refusal = refused->get_future();
    std::atomic<bool> notified{false};
    ServerOptions options;
    options.max_connections = 1;
    options.diagnostic = [&, refused](auto, const Error& e) {
        if (e.code == ErrorCode::resource_limit && !notified.exchange(true)) refused->set_value();
    };
    Server server(options);
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    auto first = std::make_unique<Peer>(server.local_port());
    const auto runtime = std::make_shared<transport::IoRuntime>();
    auto completed = std::make_shared<std::optional<Result<void>>>();
    auto extra = transport::TcpChannel::connect(runtime, "127.0.0.1", server.local_port(),
                                                [completed](auto r) { *completed = std::move(r); });
    until(runtime, [completed] { return completed->has_value(); });
    REQUIRE(**completed);
    REQUIRE(refusal.wait_for(3s) == std::future_status::ready);
    extra->close();
    runtime->finish();
    runtime->run();
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{4990}));
    first.reset();
    // 通过完成事件/原子计数观察关闭，等待仅是测试上界，不是实现的关闭算法。
    const auto deadline = IExecutor::Clock::now() + 3s;
    while (server.connections() && IExecutor::Clock::now() < deadline) std::this_thread::yield();
    REQUIRE(server.connections() == 0);
    Peer next(server.local_port());
    CHECK(next.frequency() == 4990);
}

TEST_CASE("启动失败可重试并验证串口高级配置", "[app][failure]") {
    Server server;
    CHECK_FALSE(server.start_tcp("not-an-ip", 0));
    CHECK(server.state() == ServerState::stopped);
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    Server collision;
    CHECK_FALSE(collision.start_tcp("127.0.0.1", server.local_port()));
    CHECK(collision.state() == ServerState::stopped);
    REQUIRE(server.stop());
    REQUIRE(collision.start_tcp("127.0.0.1", 0));
    REQUIRE(collision.stop());
    CHECK_FALSE(server.start_serial("nonexistent-dlt698-test-port", 9600));
    CHECK(server.state() == ServerState::stopped);
    transport::SerialLinkOptions link;
    link.set_transmit = [](bool) { return Result<void>{}; };
    auto invalid = server.start_serial("nonexistent-dlt698-test-port", {}, link);
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().code == ErrorCode::invalid_value);
    transport::SerialOptions serial;
    serial.stop_bits = transport::SerialStopBits::one_point_five;
    CHECK(server.start_serial("nonexistent-dlt698-test-port", serial, {}).error().code ==
          ErrorCode::unsupported_service);
}

TEST_CASE("预设关联无需 CONNECT 且同 OI 运行中扩充属性", "[app][tcp]") {
    Server server;
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_preset));
    Peer peer(server.local_port(), false, true);
    REQUIRE(server.set({0x2000, 2, 0}, model::Array{{model::UInt16{2400}, model::UInt16{2401},
                                                     model::UInt16{2402}}}));
    REQUIRE(server.set({0x2000, 3, 0}, model::ScalerUnit{-1, 38}));
    auto both = peer.client->get_list({{0x2000, 2, 1}, {0x2000, 3, 0}});
    REQUIRE(both);
    CHECK(std::get<model::Data>(both.value().attributes[0].result).as<model::UInt16>().value ==
          2400);
    CHECK(std::get<model::Data>(both.value().attributes[1].result).as<model::ScalerUnit>() ==
          model::ScalerUnit{-1, 38});
}

TEST_CASE("诊断回调可请求停止且阻塞停止返回 busy", "[app][lifetime]") {
    auto stopped = std::make_shared<std::promise<ErrorCode>>();
    auto future = stopped->get_future();
    std::atomic<bool> once{false};
    Server* target = nullptr;
    ServerOptions options;
    options.protocol.request_timeout = 50ms;
    options.diagnostic = [&](auto, const Error&) {
        if (once.exchange(true)) return;
        const auto result = target->stop();
        target->request_stop();
        stopped->set_value(result.error().code);
    };
    Server server(options);
    target = &server;
    REQUIRE(server.start_tcp("127.0.0.1", 0));
    auto runtime = std::make_shared<transport::IoRuntime>();
    auto completion = std::make_shared<std::optional<Result<void>>>();
    auto silent =
        transport::TcpChannel::connect(runtime, "127.0.0.1", server.local_port(),
                                       [completion](auto r) { *completion = std::move(r); });
    until(runtime, [completion] { return completion->has_value(); });
    REQUIRE(**completion);
    REQUIRE(future.wait_for(3s) == std::future_status::ready);
    CHECK(future.get() == ErrorCode::busy);
    REQUIRE(server.stop());
    silent->close();
    runtime->finish();
    runtime->run();
}

TEST_CASE("诊断回调释放最后服务器句柄仍完整收尾", "[app][lifetime]") {
    auto destroyed = std::make_shared<std::promise<void>>();
    auto future = destroyed->get_future();
    std::shared_ptr<Server> target;
    ServerOptions options;
    options.protocol.request_timeout = 50ms;
    options.diagnostic = [&](auto, const Error&) {
        if (target) {
            target.reset();
            destroyed->set_value();
        }
    };
    target = std::make_shared<Server>(options);
    REQUIRE(target->start_tcp("127.0.0.1", 0));
    const auto port = target->local_port();
    auto runtime = std::make_shared<transport::IoRuntime>();
    auto completion = std::make_shared<std::optional<Result<void>>>();
    auto silent = transport::TcpChannel::connect(
        runtime, "127.0.0.1", port, [completion](auto r) { *completion = std::move(r); });
    until(runtime, [completion] { return completion->has_value(); });
    REQUIRE(**completion);
    REQUIRE(future.wait_for(3s) == std::future_status::ready);
    // promise 建立同步关系，之后只由测试线程访问 target；不通过析构时间猜测线程退出。
    CHECK_FALSE(target);
    auto closed = std::make_shared<std::optional<Result<Bytes>>>();
    std::function<void()> read;
    read = [&] {
        silent->async_read([&](auto r) {
            if (r)
                read();
            else
                *closed = std::move(r);
        });
    };
    read();
    until(runtime, [closed] { return closed->has_value(); });
    silent->close();
    runtime->finish();
    runtime->run();
}
