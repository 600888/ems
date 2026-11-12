/** @file client_test.cpp
 * @brief 托管客户端配对、原码、超时、取消和回调生命周期的真实回环验证。
 */
#include <dlt698/app.hpp>
#include <future>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::app;
using namespace std::chrono_literals;

namespace {
std::uint16_t frequency(Client& client) {
    auto result = client.get({0x200F, 2, 0});
    REQUIRE(result);
    REQUIRE(std::holds_alternative<model::Data>(result.value()));
    return std::get<model::Data>(result.value()).as<model::UInt16>().value;
}

/** @brief 测试专用分层服务端，覆盖高层 Device 暂未提供的远端写入/方法/记录和故障。 */
struct Endpoint {
    std::shared_ptr<transport::IoRuntime> runtime = std::make_shared<transport::IoRuntime>();
    std::shared_ptr<transport::TcpListener> listener;
    std::shared_ptr<session::Session> session;
    std::shared_ptr<transport::TcpChannel> channel;
    std::thread worker;

    explicit Endpoint(std::function<void(std::shared_ptr<session::Session>)> configure = {},
                      bool silent = false) {
        auto bound = transport::TcpListener::listen(runtime, "127.0.0.1", 0);
        REQUIRE(bound);
        listener = bound.value();
        listener->async_accept([this, configure, silent](auto result) {
            if (!result) return;
            channel = std::move(result).value();
            if (silent) return;
            session::SessionOptions options;
            options.role = session::Role::server;
            session = std::make_shared<session::Session>(channel, runtime->executor(), options);
            if (configure) configure(session);
            session->start();
        });
        worker = std::thread([this] { runtime->run(); });
    }

    ~Endpoint() {
        runtime->executor()->post([this] {
            listener->close();
            if (session) session->close();
            if (channel) channel->close();
            runtime->finish();
        });
        worker.join();
    }

    std::uint16_t port() const { return listener->local_port(); }
};
}  // namespace

TEST_CASE("托管客户端默认远程连接后直接读取并重新连接", "[app][client]") {
    Server server;
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
    REQUIRE(server.start_tcp("127.0.0.1", 0));
    Client client;
    CHECK(client.get({0x200F, 2, 0}).error().code == ErrorCode::not_associated);
    auto connected = client.connect_tcp("localhost", server.local_port());
    INFO((connected ? "connected" : connected.error().context));
    REQUIRE(connected);
    CHECK(client.state() == ClientState::connected);
    CHECK(frequency(client) == 5000);
    CHECK(client.connect_tcp("127.0.0.1", server.local_port()).error().code == ErrorCode::busy);
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{4999}));
    CHECK(frequency(client) == 4999);
    auto list = client.get_list({{0x200F, 2, 0}, {0xf900, 2, 0}});
    REQUIRE(list);
    CHECK(std::get<std::uint8_t>(list.value().attributes[1].result) == 4);
    CHECK(client.set({0x200F, 2, 0}, model::UInt16{1}).value() == 3);
    REQUIRE(client.disconnect());
    CHECK(client.state() == ClientState::disconnected);
    REQUIRE(client.disconnect());
    REQUIRE(client.connect_tcp("127.0.0.1", server.local_port()));
    CHECK(frequency(client) == 4999);
    REQUIRE(server.stop());
    const auto deadline = IExecutor::Clock::now() + 3s;
    while (client.state() != ClientState::disconnected && IExecutor::Clock::now() < deadline)
        std::this_thread::yield();
    CHECK(client.state() == ClientState::disconnected);
    REQUIRE(client.disconnect());
}

TEST_CASE("本地与预设关联以及 CONNECT 拒绝原码", "[app][client][association]") {
    for (auto profile : {ConnectionProfile::local_public, ConnectionProfile::local_preset}) {
        Server server;
        REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
        REQUIRE(server.start_tcp("127.0.0.1", 0, profile));
        Client client;
        REQUIRE(client.connect_tcp("127.0.0.1", server.local_port(), profile));
        CHECK(frequency(client) == 5000);
        REQUIRE(client.disconnect());
    }
    Server server;
    REQUIRE(server.start_tcp("127.0.0.1", 0, ConnectionProfile::local_public));
    ClientOptions options;
    options.protocol.parameters.version = 0x0011;
    Client rejected(options);
    auto result =
        rejected.connect_tcp("127.0.0.1", server.local_port(), ConnectionProfile::local_public);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::association_failed);
    CHECK(result.error().remote_code == 5);
    CHECK(rejected.state() == ClientState::disconnected);
}

TEST_CASE("客户端直接 SET ACTION 及记录列表保留原始结果", "[app][client][service]") {
    auto objects = std::make_shared<service::ObjectRegistry>();
    auto memory = std::make_shared<service::MemoryObject>();
    memory->set(2, model::UInt16{42});
    memory->bind_method(1, [](const auto&, const model::Data& parameter) {
        return service::ActionValue{0, parameter};
    });
    memory->bind_record(3, [](const protocol::apdu::GetRecord& query) {
        return protocol::apdu::RecordResult{
            query.attribute, model::Rcsd{model::Oad{0xf900, 2, 0}},
            std::vector<protocol::apdu::RecordRow>(400, {model::UInt16{42}})};
    });
    REQUIRE(objects->register_object(
        {0xf900,
         "可写测试",
         {{2, model::DataType::uint16, true, true}, {3, model::DataType::null, true, false, true}},
         {{1, model::DataType::uint16, model::DataType::uint16}}},
        memory));
    Endpoint server([objects](auto session) { service::ServerService service(session, objects); });
    Client client;
    REQUIRE(client.connect_tcp("127.0.0.1", server.port(), ConnectionProfile::local_public));
    CHECK(client.set({0xf900, 2, 0}, model::UInt16{24}).value() == 0);
    auto set_list =
        client.set_list({{{0xf900, 2, 0}, model::UInt16{25}}, {{0xf900, 7, 0}, model::UInt16{1}}});
    REQUIRE(set_list);
    CHECK(set_list.value().attributes[0].dar == 0);
    CHECK(set_list.value().attributes[1].dar == 4);
    auto action = client.action({0xf900, 1, 0}, model::UInt16{31});
    REQUIRE(action);
    CHECK(action.value().dar == 0);
    CHECK(action.value().data->as<model::UInt16>().value == 31);
    auto actions = client.action_list({{{0xf900, 1, 0}, model::UInt16{32}}});
    REQUIRE(actions);
    CHECK(actions.value().methods[0].data->as<model::UInt16>().value == 32);
    protocol::apdu::GetRecord query{{0xf900, 3, 0}, model::SelectAll{}, {}};
    auto record = client.get_record(query);
    REQUIRE(record);
    CHECK(std::get<std::vector<protocol::apdu::RecordRow>>(record.value().result).size() == 400);
    auto records = client.get_record_list({query});
    REQUIRE(records);
    CHECK(records.value().records.size() == 1);
    CHECK(std::get<std::vector<protocol::apdu::RecordRow>>(records.value().records[0].result)
              .size() == 400);
    REQUIRE(client.disconnect());
}

TEST_CASE("等待 LINK 超时和取消都结束建连且可重试", "[app][client][timeout]") {
    ClientOptions options;
    options.login_timeout = 60ms;
    Endpoint silent({}, true);
    Client client(options);
    auto result = client.connect_tcp("127.0.0.1", silent.port());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::timeout);
    CHECK(result.error().context == "LINK login timeout");
    CHECK(client.state() == ClientState::disconnected);
    Endpoint again({}, true);
    auto pending = std::async(std::launch::async,
                              [&] { return client.connect_tcp("127.0.0.1", again.port()); });
    const auto deadline = IExecutor::Clock::now() + 3s;
    while (client.state() != ClientState::connecting && IExecutor::Clock::now() < deadline)
        std::this_thread::yield();
    REQUIRE(client.state() == ClientState::connecting);
    client.request_disconnect();
    REQUIRE(pending.wait_for(3s) == std::future_status::ready);
    CHECK(pending.get().error().code == ErrorCode::cancelled);
    REQUIRE(client.disconnect());
    Server available;
    REQUIRE(available.start_tcp("127.0.0.1", 0));
    REQUIRE(client.connect_tcp("127.0.0.1", available.local_port()));
    REQUIRE(client.disconnect());
}

TEST_CASE("客户端在途请求冲突返回 busy 且断开唤醒等待", "[app][client][cancel]") {
    auto seen = std::make_shared<std::promise<void>>();
    auto future = seen->get_future();
    Endpoint server([seen](auto session) {
        session->set_request_handler([seen](const auto& request) {
            seen->set_value();
            // 错误 OAD 不匹配请求，验证客户端不能把迟到/不相关响应当作完成。
            return protocol::apdu::GetResponse{
                request.piid, false, {{{0xf901, 2, 0}, std::uint8_t{4}}}, {}};
        });
    });
    Client client;
    REQUIRE(client.connect_tcp("127.0.0.1", server.port(), ConnectionProfile::local_public));
    auto reading = std::async(std::launch::async, [&] { return client.get({0xf900, 2, 0}); });
    REQUIRE(future.wait_for(3s) == std::future_status::ready);
    CHECK(client.get({0xf900, 2, 0}).error().code == ErrorCode::busy);
    REQUIRE(client.disconnect());
    REQUIRE(reading.wait_for(3s) == std::future_status::ready);
    CHECK_FALSE(reading.get());
    CHECK(client.state() == ClientState::disconnected);
}

TEST_CASE("回调同步 busy 与最后句柄释放", "[app][client][lifetime]") {
    auto busy = std::make_shared<std::promise<ErrorCode>>();
    auto busy_result = busy->get_future();
    std::shared_ptr<Client> client;
    ClientOptions options;
    options.protocol.request_timeout = 60ms;
    options.diagnostic = [&](const Error&) {
        if (!client) return;
        const auto code = client->get({0xf900, 2, 0}).error().code;
        CHECK(client->disconnect().error().code == ErrorCode::busy);
        client.reset();
        busy->set_value(code);
    };
    Endpoint server([](auto session) {
        session->set_request_handler([](const auto& request) {
            return protocol::apdu::GetResponse{
                request.piid, false, {{{0xf901, 2, 0}, std::uint8_t{4}}}, {}};
        });
    });
    client = std::make_shared<Client>(options);
    REQUIRE(client->connect_tcp("127.0.0.1", server.port(), ConnectionProfile::local_public));
    auto result = client->get({0xf900, 2, 0});
    REQUIRE_FALSE(result);
    REQUIRE(busy_result.wait_for(3s) == std::future_status::ready);
    CHECK(busy_result.get() == ErrorCode::busy);
    // 回调因不匹配响应触发并销毁句柄，取消完成仍能送达同步等待方。
    CHECK_FALSE(client);
}

TEST_CASE("CONNECT 及业务请求超时完整关闭，远端 ERROR 保留原码", "[app][client][timeout]") {
    ClientOptions options;
    options.protocol.request_timeout = 60ms;
    Client client(options);
    Endpoint silent({}, true);
    auto connected =
        client.connect_tcp("127.0.0.1", silent.port(), ConnectionProfile::local_public);
    REQUIRE_FALSE(connected);
    CHECK(connected.error().code == ErrorCode::timeout);
    CHECK(client.state() == ClientState::disconnected);
    Endpoint mismatched([](auto session) {
        session->set_request_handler([](const auto& request) {
            return protocol::apdu::GetResponse{
                request.piid, false, {{{0xf901, 2, 0}, std::uint8_t{4}}}, {}};
        });
    });
    REQUIRE(client.connect_tcp("127.0.0.1", mismatched.port(), ConnectionProfile::local_public));
    auto read = client.get({0xf900, 2, 0});
    REQUIRE_FALSE(read);
    CHECK(read.error().code == ErrorCode::timeout);
    REQUIRE(client.disconnect());
    Endpoint error([](auto session) {
        session->set_request_handler([](const auto&) -> protocol::apdu::GetResponse {
            throw std::runtime_error("server failure");
        });
    });
    REQUIRE(client.connect_tcp("127.0.0.1", error.port(), ConnectionProfile::local_public));
    auto rejected = client.get({0xf900, 2, 0});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == ErrorCode::remote_error);
    CHECK(rejected.error().remote_code == 255);
    REQUIRE(client.disconnect());
}

TEST_CASE("客户端参数与串口失败不残留运行资源", "[app][client][failure]") {
    Client client;
    CHECK(client.connect_tcp("", 6980).error().code == ErrorCode::invalid_value);
    CHECK(client.connect_tcp("127.0.0.1", 0).error().code == ErrorCode::invalid_value);
    CHECK_FALSE(client.open_serial("nonexistent-dlt698-test-port"));
    CHECK(client.state() == ClientState::disconnected);
    transport::SerialLinkOptions link;
    link.set_transmit = [](bool) { return Result<void>{}; };
    CHECK(client.open_serial("nonexistent-dlt698-test-port", {}, link).error().code ==
          ErrorCode::invalid_value);
    transport::SerialOptions serial;
    serial.stop_bits = transport::SerialStopBits::one_point_five;
    CHECK(client.open_serial("nonexistent-dlt698-test-port", serial, {}).error().code ==
          ErrorCode::unsupported_service);
    ClientOptions options;
    options.transport_timeout = 0ms;
    CHECK_THROWS_AS(Client(options), std::invalid_argument);
}

TEST_CASE("TCP 失败可重新连接且对端释放通知结束高层关联", "[app][client][failure]") {
    std::uint16_t closed_port = 0;
    {
        Endpoint closed({}, true);
        closed_port = closed.port();
    }
    Client client;
    auto failed = client.connect_tcp("127.0.0.1", closed_port, ConnectionProfile::local_public);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::io_error);
    CHECK(client.state() == ClientState::disconnected);
    Endpoint server;
    REQUIRE(client.connect_tcp("127.0.0.1", server.port(), ConnectionProfile::local_public));
    auto sent = std::make_shared<std::promise<Result<void>>>();
    auto future = sent->get_future();
    server.runtime->executor()->post([&server, sent] {
        protocol::apdu::ReleaseNotification notice{0,
                                                   model::DateTimeS{{7, 0xea, 10, 7, 0, 0, 0}},
                                                   model::DateTimeS{{7, 0xea, 10, 7, 0, 0, 1}},
                                                   {}};
        auto payload = protocol::apdu::encode_connection(notice);
        if (!payload) {
            sent->set_value(payload.error());
            return;
        }
        protocol::link::Frame frame;
        frame.control = 0x83;  // 协议服务器主动发送的释放通知，与客户机请求响应方向不同。
        frame.payload = std::move(payload).value();
        auto bytes = protocol::link::encode_frame(frame);
        if (!bytes)
            sent->set_value(bytes.error());
        else
            server.channel->async_write(std::move(bytes).value(), [sent](auto result) {
                sent->set_value(std::move(result));
            });
    });
    REQUIRE(future.wait_for(3s) == std::future_status::ready);
    REQUIRE(future.get());
    const auto deadline = IExecutor::Clock::now() + 3s;
    while (client.state() != ClientState::disconnected && IExecutor::Clock::now() < deadline)
        std::this_thread::yield();
    CHECK(client.state() == ClientState::disconnected);
    CHECK(client.get({0x200F, 2, 0}).error().code == ErrorCode::not_associated);
    REQUIRE(client.disconnect());
}
