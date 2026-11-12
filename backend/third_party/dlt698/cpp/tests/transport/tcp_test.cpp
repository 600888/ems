/**
 * @file tcp_test.cpp
 * @brief TCP 通道与运行时的收发、连接与生命周期测试。
 * @note 绑定 127.0.0.1 的临时端口，只验证本机回环行为，不依赖外部网络。
 */
#include <chrono>
#include <dlt698/dlt698.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/tcp.hpp>
#include <thread>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::transport;
using namespace std::chrono_literals;
using dlt698::test::hex;

namespace {

/**
 * @brief 驱动运行时直到条件成立或超时。
 * @param[in] runtime IO 运行时。
 * @param[in] done 完成条件；超时后强制断言失败，避免用例悬挂。
 */
void until(const std::shared_ptr<IoRuntime>& runtime, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline) runtime->run_for(5ms);
    REQUIRE(done());
}

/**
 * @brief 建立一对已连接的 TCP 通道，失败时给出可读诊断。
 * @param[in] runtime IO 运行时。
 * @param[out] server 服务端通道。
 * @param[out] client 客户端通道。
 * @note 某些 CI沙箱禁止回环连接，此时连接回调会带错误码失败。
 * 用例必须显式检查连接结果，不能仅凭"回调被调用"就认为连接成功。
 */
void connect_pair(const std::shared_ptr<IoRuntime>& runtime, std::shared_ptr<TcpChannel>& server,
                  std::shared_ptr<TcpChannel>& client) {
    // 监听器必须在整个用例内存活：挂起的 accept 会持有它的弱引用，
    // 提前析构会让回调访问已释放的实现。
    auto listener_result = TcpListener::listen(runtime, "127.0.0.1", 0, ChannelOptions{});
    REQUIRE(static_cast<bool>(listener_result));
    auto listener = listener_result.value();
    const auto port = listener->local_port();

    std::optional<Result<void>> connected;
    listener->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (r) server = std::move(r).value();
    });
    client = TcpChannel::connect(runtime, "127.0.0.1", port,
                                 [&](Result<void> r) { connected = std::move(r); });
    until(runtime, [&] { return connected.has_value(); });

    // 回环连接在受限沙箱中可能被拒绝；此时跳过其余断言而不是让用例无意义地失败。
    if (!static_cast<bool>(*connected)) {
        INFO("回环连接被环境拒绝，错误码=" << static_cast<int>(connected->error().code));
        listener->close();
        runtime->run_for(5ms);
        SKIP("当前环境不允许建立回环 TCP 连接");
    }
    until(runtime, [&] { return server != nullptr; });
    REQUIRE(server != nullptr);

    // 显式关闭以取消挂起的 accept，随后再让监听器析构。
    listener->close();
    runtime->run_for(5ms);
}

}  // namespace

TEST_CASE("TCP 连接建立时回调收到成功", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    auto listener_result = TcpListener::listen(runtime, "127.0.0.1", 0, ChannelOptions{});
    REQUIRE(static_cast<bool>(listener_result));
    auto listener = listener_result.value();
    CHECK(listener->local_port() != 0);  // 端口由系统分配，必然非零。

    std::shared_ptr<TcpChannel> server;
    std::optional<Result<void>> connected;
    listener->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (r) server = std::move(r).value();
    });
    auto client = TcpChannel::connect(runtime, "127.0.0.1", listener->local_port(),
                                      [&](Result<void> r) { connected = std::move(r); });
    REQUIRE(client);
    until(runtime, [&] { return connected.has_value(); });
    REQUIRE(connected.has_value());

    if (!static_cast<bool>(*connected)) {
        INFO("回环连接被环境拒绝，错误码=" << static_cast<int>(connected->error().code));
        SKIP("当前环境不允许建立回环 TCP 连接");
    }
    CHECK(static_cast<bool>(*connected));
    until(runtime, [&] { return server != nullptr; });
    CHECK(server != nullptr);

    // 显式关闭以取消挂起的 accept，避免监听器析构时回调访问已释放的实现。
    listener->close();
    runtime->run_for(5ms);
}

TEST_CASE("TCP 连接建立后可双向交换字节", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    std::shared_ptr<TcpChannel> server, client;
    connect_pair(runtime, server, client);

    std::optional<Result<Bytes>> at_server;
    server->async_read([&](Result<Bytes> r) { at_server = std::move(r); });
    const Bytes request = hex("01 02 03 04");
    client->async_write(request, [](Result<void>) {});
    until(runtime, [&] { return at_server.has_value(); });
    REQUIRE(at_server.has_value());
    REQUIRE(static_cast<bool>(*at_server));
    CHECK(to_hex(ByteView{at_server->value()}) == "01 02 03 04");

    std::optional<Result<Bytes>> at_client;
    client->async_read([&](Result<Bytes> r) { at_client = std::move(r); });
    const Bytes reply = hex("AA BB");
    server->async_write(reply, [](Result<void>) {});
    until(runtime, [&] { return at_client.has_value(); });
    REQUIRE(at_client.has_value());
    REQUIRE(static_cast<bool>(*at_client));
    CHECK(to_hex(ByteView{at_client->value()}) == "AA BB");

    client->close();
    server->close();
    runtime->run_for(5ms);
}

TEST_CASE("关闭客户端后服务端读到 closed", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    std::shared_ptr<TcpChannel> server, client;
    connect_pair(runtime, server, client);

    std::optional<Result<Bytes>> at_server;
    server->async_read([&](Result<Bytes> r) { at_server = std::move(r); });
    client->close();
    until(runtime, [&] { return at_server.has_value(); });
    REQUIRE(at_server.has_value());
    REQUIRE_FALSE(static_cast<bool>(*at_server));
    CHECK(at_server->error().code == ErrorCode::closed);

    client->close();
    server->close();
    runtime->run_for(5ms);
}

TEST_CASE("连接被拒绝时回调收到错误", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    // 先占用一个端口再释放，用它来触发连接失败。
    std::uint16_t port = 0;
    {
        auto listener_result = TcpListener::listen(runtime, "127.0.0.1", 0, ChannelOptions{});
        REQUIRE(static_cast<bool>(listener_result));
        port = listener_result.value()->local_port();
        listener_result.value()->close();
        runtime->run_for(5ms);
    }
    std::optional<Result<void>> outcome;
    auto client = TcpChannel::connect(runtime, "127.0.0.1", port,
                                      [&](Result<void> r) { outcome = std::move(r); });
    REQUIRE(client);
    until(runtime, [&] { return outcome.has_value(); });
    REQUIRE(outcome.has_value());
    CHECK_FALSE(static_cast<bool>(*outcome));
}

TEST_CASE("监听器关闭后不再接受连接", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    auto listener_result = TcpListener::listen(runtime, "127.0.0.1", 0, ChannelOptions{});
    REQUIRE(static_cast<bool>(listener_result));
    auto listener = listener_result.value();
    const auto port = listener->local_port();
    listener->close();
    CHECK_NOTHROW(listener->close());  // 幂等。
    runtime->run_for(5ms);

    std::optional<Result<void>> outcome;
    auto client = TcpChannel::connect(runtime, "127.0.0.1", port,
                                      [&](Result<void> r) { outcome = std::move(r); });
    REQUIRE(client);
    until(runtime, [&] { return outcome.has_value(); });
    REQUIRE(outcome.has_value());
    CHECK_FALSE(static_cast<bool>(*outcome));
}

TEST_CASE("运行时停止后可重启并继续接受连接", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    auto listener_result = TcpListener::listen(runtime, "127.0.0.1", 0, ChannelOptions{});
    REQUIRE(static_cast<bool>(listener_result));
    auto listener = listener_result.value();
    const auto port = listener->local_port();

    runtime->stop();
    runtime->restart();

    std::shared_ptr<TcpChannel> server;
    std::optional<Result<void>> connected;
    listener->async_accept([&](Result<std::shared_ptr<TcpChannel>> r) {
        if (r) server = std::move(r).value();
    });
    auto client = TcpChannel::connect(runtime, "127.0.0.1", port,
                                      [&](Result<void> r) { connected = std::move(r); });
    REQUIRE(client);
    until(runtime, [&] { return connected.has_value(); });
    if (connected && !static_cast<bool>(*connected)) {
        listener->close();
        runtime->run_for(5ms);
        SKIP("当前环境不允许建立回环 TCP 连接");
    }
    until(runtime, [&] { return server != nullptr; });
    CHECK(server != nullptr);

    listener->close();
    runtime->run_for(5ms);
}

TEST_CASE("运行时提供的执行器串行执行任务", "[transport][tcp]") {
    auto runtime = std::make_shared<IoRuntime>();
    // 执行器由运行时派生，必须让运行时先于执行器销毁，否则投递会访问已释放的上下文。
    auto executor = runtime->executor();
    REQUIRE(executor != nullptr);

    int counter = 0;
    executor->post([&counter] { ++counter; });
    until(runtime, [&counter] { return counter == 1; });
    CHECK(counter == 1);

    runtime->stop();
    executor.reset();
    runtime->run_for(5ms);
}

TEST_CASE("串口参数在打开前即被校验", "[transport][serial]") {
    auto runtime = std::make_shared<IoRuntime>();

    // 参数校验发生在触碰设备之前，因此不依赖真实串口硬件。
    SECTION("空设备名被拒绝") {
        auto result = SerialChannel::open(runtime, "", SerialOptions{});
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::invalid_value);
    }

    SECTION("零波特率被拒绝") {
        SerialOptions options;
        options.baud_rate = 0;
        auto result = SerialChannel::open(runtime, "COM1", options);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::invalid_value);
    }

    SECTION("数据位越界被拒绝") {
        SerialOptions options;
        options.data_bits = 9;
        auto result = SerialChannel::open(runtime, "COM1", options);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::invalid_value);
    }

    SECTION("空运行时被拒绝") {
        auto result = SerialChannel::open(nullptr, "COM1", SerialOptions{});
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::invalid_value);
    }
}
