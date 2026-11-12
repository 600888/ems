/**
 * @file session_test.cpp
 * @brief 会话状态机、公共连接、事务匹配、诊断回调与分帧重组的单元测试。
 * @note 全部用例使用 ManualExecutor 虚拟时钟与内存通道，不依赖真实时间或设备互操作。
 */
#include <chrono>
#include <dlt698/session/session.hpp>
#include <dlt698/transport/memory.hpp>
#include <functional>
#include <stdexcept>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::session;
namespace apdu = protocol::apdu;
// POSIX 在全局声明 link()，链路层别名使用不同名称以避免平台相关的重名冲突。
namespace link_layer = protocol::link;
using namespace std::chrono_literals;

namespace {

/// 把状态转换成可读文本，断言失败时直接暴露实际状态。
const char* state_name(State state) {
    switch (state) {
        case State::disconnected:
            return "disconnected";
        case State::preconnected:
            return "preconnected";
        case State::associating:
            return "associating";
        case State::associated:
            return "associated";
        case State::releasing:
            return "releasing";
        case State::closed:
            return "closed";
    }
    return "unknown";
}

/// 断言状态符合预期，失败时打印两个状态的名称。
void check_state(const Session& session, State expected) {
    INFO("实际=" << state_name(session.state()) << " 预期=" << state_name(expected));
    CHECK(session.state() == expected);
}

/**
 * @brief 一对通过内存通道连接的会话，用于驱动完整状态机。
 * @note 通道端点保留在成员中，便于直接注入线上字节模拟损坏帧与迟到响应。
 */
struct Peers {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<transport::MemoryChannel> client_channel, server_channel;
    std::shared_ptr<Session> client, server;
    std::vector<Error> diagnostics;

    /**
     * @brief 构造并启动两侧会话。
     * @param preset true 时双方预设关联，跳过 CONNECT 以聚焦事务行为。
     * @param client_first false 时交换绑定端点，用于验证协议角色与拨号方向独立。
     * @param linked true 时服务器要求先完成 LINK 登录。
     */
    Peers(bool preset = true, bool client_first = true, bool linked = false) {
        auto channels = transport::MemoryChannel::pair(executor);
        client_channel = channels.first;
        server_channel = channels.second;
        SessionOptions client_options, server_options;
        client_options.role = Role::client;
        server_options.role = Role::server;
        client_options.preset_association = server_options.preset_association = preset;
        // 服务器要求登录时，登录成功前保持 disconnected。
        server_options.require_login = linked;
        if (client_first) {
            client = std::make_shared<Session>(client_channel, executor, client_options);
            server = std::make_shared<Session>(server_channel, executor, server_options);
        } else {
            // 协议服务器绑定到通常由客户机主动拨号的那一端。
            server = std::make_shared<Session>(client_channel, executor, server_options);
            client = std::make_shared<Session>(server_channel, executor, client_options);
        }
        client->set_diagnostic_handler(
            [this](const Error& error) { diagnostics.push_back(error); });
        server->set_diagnostic_handler(
            [this](const Error& error) { diagnostics.push_back(error); });
        client->start();
        server->start();
        executor->run_ready();
    }

    /// 关闭两侧会话并驱动剩余回调，避免析构时留下未交付的断言。
    ~Peers() {
        client->close();
        server->close();
        executor->run_ready();
    }

    /// 让服务器对每个属性返回给定 Data，便于验证逐项结果。
    void serve_values(const model::Data& value) {
        server->set_request_handler([value](const apdu::GetRequest& request) {
            apdu::GetResponse response;
            for (const auto& attribute : request.attributes)
                response.attributes.push_back({attribute, value});
            return response;
        });
        executor->run_ready();
    }

    /// 直接向指定通道写入已编码的线上字节。
    void inject(const std::shared_ptr<transport::MemoryChannel>& from, const Bytes& bytes) {
        from->async_write(bytes, [](Result<void>) {});
        executor->run_ready();
    }

    /// 构造一帧用户数据，control 与 address 允许调用方伪造方向位和地址。
    Bytes user_frame(std::uint8_t control, const link_layer::ServerAddress& address,
                     const Bytes& payload) {
        auto encoded = link_layer::encode_frame(link_layer::Frame{control, address, 0, payload});
        REQUIRE(static_cast<bool>(encoded));
        return std::move(encoded).value();
    }

    /// 查找指定分类与上下文的诊断；找不到返回 nullptr。
    const Error* diagnostic(ErrorCode code, std::string_view context) const {
        for (const auto& error : diagnostics)
            if (error.code == code && error.context == context) return &error;
        return nullptr;
    }
};

/// 生成一个无人应答的孤立客户机会话，用于观察在途事务与中间状态。
struct LoneClient {
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<transport::MemoryChannel> near, far;
    std::shared_ptr<Session> session;

    /// 构造孤立会话；preset=true 时直接进入已关联，否则停在预连接。
    explicit LoneClient(bool preset = true, std::chrono::milliseconds timeout = 5000ms) {
        auto channels = transport::MemoryChannel::pair(executor);
        near = channels.first;
        far = channels.second;
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = preset;
        options.request_timeout = timeout;
        session = std::make_shared<Session>(near, executor, options);
        session->start();
        executor->run_ready();
    }
};

}  // namespace

TEST_CASE("会话状态机按角色迁移", "[session][state]") {
    SECTION("预设关联使双方直接进入已关联") {
        Peers peers(/*preset=*/true);
        check_state(*peers.client, State::associated);
        check_state(*peers.server, State::associated);
    }

    SECTION("未预设关联时双方处于预连接") {
        Peers peers(/*preset=*/false);
        check_state(*peers.client, State::preconnected);
        check_state(*peers.server, State::preconnected);
    }

    SECTION("客户机经预连接、关联、释放回到预连接") {
        Peers peers(/*preset=*/false);
        std::optional<Result<apdu::ConnectResponse>> response;
        peers.client->async_connect([&](auto result) { response = std::move(result); });
        // 投递但未执行时状态不变，这是原子发布语义的体现。
        check_state(*peers.client, State::preconnected);
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        check_state(*peers.client, State::associated);
        check_state(*peers.server, State::associated);

        std::optional<Result<void>> released;
        peers.client->async_release([&](auto result) { released = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(released);
        CHECK(static_cast<bool>(*released));
        // 释放后两侧回到预连接，物理通道仍可继续使用。
        check_state(*peers.client, State::preconnected);
        check_state(*peers.server, State::preconnected);
    }

    SECTION("无人应答时停留在关联中") {
        LoneClient lone(/*preset=*/false);
        check_state(*lone.session, State::preconnected);
        std::optional<Result<apdu::ConnectResponse>> response;
        lone.session->async_connect([&](auto result) { response = std::move(result); });
        lone.executor->run_ready();
        // 事务在途但对端不存在，状态明确停在关联中。
        check_state(*lone.session, State::associating);
        CHECK_FALSE(response);
    }

    SECTION("释放请求在途时状态为 releasing") {
        LoneClient lone(/*preset=*/true);
        check_state(*lone.session, State::associated);
        std::optional<Result<void>> released;
        lone.session->async_release([&](auto result) { released = std::move(result); });
        lone.executor->run_ready();
        check_state(*lone.session, State::releasing);
        CHECK_FALSE(released);
    }

    SECTION("重复 start 没有额外效果") {
        Peers peers(/*preset=*/true);
        peers.client->start();
        peers.client->start();
        peers.executor->run_ready();
        check_state(*peers.client, State::associated);
    }

    SECTION("关闭幂等且不可逆") {
        Peers peers(/*preset=*/true);
        peers.client->close();
        peers.executor->run_ready();
        check_state(*peers.client, State::closed);
        CHECK_NOTHROW(peers.client->close());
        peers.executor->run_ready();
        check_state(*peers.client, State::closed);
        // 关闭后不能重新建立关联。
        std::optional<Result<apdu::ConnectResponse>> rejected;
        peers.client->async_connect([&](auto result) { rejected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(rejected);
        test::require_error(*rejected, ErrorCode::closed);
    }
}

TEST_CASE("协议角色与通道拨号方向相互独立", "[session][state]") {
    SECTION("服务器角色绑定到通常由客户机拨号的那一端") {
        // MemoryChannel::pair 的 first 端模拟主动拨号方，这里交给协议服务器角色。
        Peers peers(/*preset=*/false, /*client_first=*/false);
        std::optional<Result<apdu::ConnectResponse>> response;
        peers.client->async_connect([&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        check_state(*peers.client, State::associated);
        check_state(*peers.server, State::associated);
        // 关联后业务数据照常往返，与拨号方向无关。
        peers.serve_values(model::Data{model::UInt16{7}});
        std::optional<Result<apdu::GetResponse>> value;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { value = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(value);
        REQUIRE(static_cast<bool>(*value));
        REQUIRE(value->value().attributes.size() == 1);
        CHECK(std::get<model::Data>(value->value().attributes[0].result) ==
              model::Data{model::UInt16{7}});
    }
}

TEST_CASE("公共连接的成功与拒绝", "[session][connect]") {
    SECTION("协商成功时双方进入已关联并保留远端参数") {
        Peers peers(/*preset=*/false);
        std::optional<Result<apdu::ConnectResponse>> response;
        peers.client->async_connect([&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value().result == 0);
        CHECK(response->value().parameters.version == 0x0010);
        check_state(*peers.client, State::associated);
    }

    SECTION("远端拒绝时外层 Result 成功但响应结果非零") {
        // 版本不匹配是服务器唯一的拒绝途径，这里单独构造两侧版本不同的会话。
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions client_options, server_options;
        server_options.role = Role::server;
        server_options.parameters.version = 0x0020;  // 与客户机的 0x0010 不同。
        auto client = std::make_shared<Session>(channels.first, executor, client_options);
        auto server = std::make_shared<Session>(channels.second, executor, server_options);
        client->start();
        server->start();
        executor->run_ready();
        std::optional<Result<apdu::ConnectResponse>> response;
        client->async_connect([&](auto result) { response = std::move(result); });
        executor->run_ready();
        REQUIRE(response);
        // 事务本身成功，拒绝原因保留在远端结果码中：只看外层 Result 会误判。
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value().result == 5);
        check_state(*client, State::preconnected);
        client->close();
        server->close();
        executor->run_ready();
    }

    SECTION("服务器要求登录时未登录的关联被拒绝") {
        Peers peers(/*preset=*/false, /*client_first=*/true, /*linked=*/true);
        check_state(*peers.server, State::disconnected);
        std::optional<Result<apdu::ConnectResponse>> rejected;
        peers.client->async_connect([&](auto result) { rejected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(rejected);
        // 服务器在未登录状态下以 255 拒绝，而不是丢弃请求。
        REQUIRE(static_cast<bool>(*rejected));
        CHECK(rejected->value().result == 255);
        check_state(*peers.server, State::disconnected);
    }

    SECTION("登录成功后可以建立关联") {
        Peers peers(/*preset=*/false, /*client_first=*/true, /*linked=*/true);
        std::optional<Result<apdu::LinkResponse>> login;
        peers.server->async_link(apdu::LinkRequestType::login, 0,
                                 [&](auto result) { login = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(login);
        REQUIRE(static_cast<bool>(*login));
        check_state(*peers.server, State::preconnected);
        std::optional<Result<apdu::ConnectResponse>> response;
        peers.client->async_connect([&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        CHECK(response->value().result == 0);
        check_state(*peers.client, State::associated);
    }

    SECTION("角色限制阻止反向发起连接管理") {
        Peers peers(/*preset=*/true);
        // LINK 由协议服务器发起，客户机角色无权发起。
        std::optional<Result<apdu::LinkResponse>> link_result;
        peers.client->async_link(apdu::LinkRequestType::login, 0,
                                 [&](auto result) { link_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(link_result);
        test::require_error(*link_result, ErrorCode::invalid_value);
        CHECK(link_result->error().context == "LINK role/state");
        // CONNECT 由协议客户机发起，服务器角色无权发起。
        std::optional<Result<apdu::ConnectResponse>> connect_result;
        peers.server->async_connect([&](auto result) { connect_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(connect_result);
        test::require_error(*connect_result, ErrorCode::not_associated);
        CHECK(connect_result->error().context == "CONNECT state/role");
    }

    SECTION("已关联的会话不能重复协商") {
        Peers peers(/*preset=*/true);
        std::optional<Result<apdu::ConnectResponse>> rejected;
        peers.client->async_connect([&](auto result) { rejected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(rejected);
        test::require_error(*rejected, ErrorCode::not_associated);
        CHECK(rejected->error().context == "CONNECT state/role");
    }
}

TEST_CASE("读写方法事务返回逐项业务结果", "[session][service]") {
    Peers peers(/*preset=*/true);

    SECTION("GET 保留每个属性的 Data 或 DAR") {
        peers.server->set_request_handler([](const apdu::GetRequest& request) {
            apdu::GetResponse response;
            for (const auto& attribute : request.attributes) {
                if (attribute.oi == 0x2000)
                    response.attributes.push_back({attribute, model::Data{model::UInt16{220}}});
                else
                    response.attributes.push_back({attribute, std::uint8_t{4}});
            }
            return response;
        });
        peers.executor->run_ready();
        std::optional<Result<apdu::GetResponse>> response;
        peers.client->async_get({{0x2000, 2, 0}, {0xf001, 2, 0}}, true,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        // 外层 Result 成功，逐项业务结果才是判断依据。
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().attributes.size() == 2);
        CHECK(response->value().list);
        CHECK(std::get<model::Data>(response->value().attributes[0].result) ==
              model::Data{model::UInt16{220}});
        REQUIRE(std::holds_alternative<std::uint8_t>(response->value().attributes[1].result));
        CHECK(std::get<std::uint8_t>(response->value().attributes[1].result) == 4);
    }

    SECTION("GET 普通形式必须恰好一个属性") {
        std::optional<Result<apdu::GetResponse>> rejected;
        peers.client->async_get({{0x2000, 2, 0}, {0x2001, 2, 0}}, false,
                                [&](auto result) { rejected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(rejected);
        // 数量不合法属于长度类错误，在编码阶段就被拒绝。
        test::require_error(*rejected, ErrorCode::invalid_length);
        CHECK(rejected->error().context == "GET count");
    }

    SECTION("未注册处理器时服务器逐项返回未定义 DAR") {
        std::optional<Result<apdu::GetResponse>> response;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().attributes.size() == 1);
        REQUIRE(std::holds_alternative<std::uint8_t>(response->value().attributes[0].result));
        // 没有处理器时属性未定义，服务器返回 DAR=4 而不是事务失败。
        CHECK(std::get<std::uint8_t>(response->value().attributes[0].result) == 4);
    }

    SECTION("SET 逐项返回 DAR 且不自动重试") {
        unsigned calls = 0;
        peers.server->set_set_handler([&](const apdu::SetRequest& request) {
            ++calls;
            apdu::SetResponse response;
            for (const auto& item : request.attributes)
                response.attributes.push_back({item.attribute, item.attribute.oi == 0x2000
                                                                   ? std::uint8_t{0}
                                                                   : std::uint8_t{3}});
            return response;
        });
        peers.executor->run_ready();
        std::optional<Result<apdu::SetResponse>> response;
        peers.client->async_set({{model::Oad{0x2000, 2, 0}, model::Data{model::UInt16{1}}},
                                 {model::Oad{0xf001, 2, 0}, model::Data{model::UInt16{1}}}},
                                true, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().attributes.size() == 2);
        CHECK(response->value().list);
        CHECK(response->value().attributes[0].dar == 0);
        CHECK(response->value().attributes[1].dar == 3);
        // 有副作用的写入只发送一次，拒绝也不会重试。
        CHECK(calls == 1);
    }

    SECTION("ACTION 保留方法结果与可选返回数据") {
        peers.server->set_action_handler([](const apdu::ActionRequest& request) {
            apdu::ActionResponse response;
            for (const auto& item : request.methods) {
                if (item.method.oi == 0x4000)
                    response.methods.push_back({item.method, 0, model::Data{model::UInt8{9}}});
                else
                    response.methods.push_back({item.method, 4, {}});
            }
            return response;
        });
        peers.executor->run_ready();
        std::optional<Result<apdu::ActionResponse>> response;
        peers.client->async_action({{model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{1}}},
                                    {model::Omd{0xf001, 1, 0}, model::Data{model::UInt8{1}}}},
                                   true, [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(response->value().methods.size() == 2);
        CHECK(response->value().list);
        CHECK(response->value().methods[0].dar == 0);
        REQUIRE(response->value().methods[0].data.has_value());
        CHECK(*response->value().methods[0].data == model::Data{model::UInt8{9}});
        // 没有返回数据与返回 NULL Data 是不同结果。
        CHECK(response->value().methods[1].dar == 4);
        CHECK_FALSE(response->value().methods[1].data.has_value());
    }

    SECTION("业务失败必须看逐项 DAR 而不是外层 Result") {
        // 服务器对属性返回 DAR=4：事务成功，业务上却是未定义对象。
        std::optional<Result<apdu::GetResponse>> response;
        peers.client->async_get({{0xf001, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(std::holds_alternative<std::uint8_t>(response->value().attributes[0].result));
        CHECK(std::get<std::uint8_t>(response->value().attributes[0].result) == 4);
    }
}

TEST_CASE("单在途事务拒绝并发请求", "[session][transaction]") {
    LoneClient lone(/*preset=*/true);
    std::optional<Result<apdu::GetResponse>> first, second;

    // 对端不存在，第一个请求保持在途。
    lone.session->async_get({{0x2000, 2, 0}}, false,
                            [&](auto result) { first = std::move(result); });
    lone.executor->run_ready();
    CHECK_FALSE(first);
    check_state(*lone.session, State::associated);

    lone.session->async_get({{0x2001, 2, 0}}, false,
                            [&](auto result) { second = std::move(result); });
    lone.executor->run_ready();
    REQUIRE(second);
    // 第二个请求立即被拒绝，不排队等待。
    test::require_error(*second, ErrorCode::busy);
    CHECK(second->error().context == "session transaction");
    // 被拒绝的请求没有上线，第一个仍在等待。
    CHECK_FALSE(first);

    SECTION("取消后第一个事务以 cancelled 结束") {
        lone.session->cancel();
        lone.executor->run_ready();
        REQUIRE(first);
        test::require_error(*first, ErrorCode::cancelled);
        check_state(*lone.session, State::closed);
    }
}

TEST_CASE("未关联与已关闭状态拒绝业务事务", "[session][state]") {
    Peers peers(/*preset=*/false);

    SECTION("未关联时读写全部返回 not_associated") {
        std::optional<Result<apdu::GetResponse>> get_result;
        std::optional<Result<apdu::SetResponse>> set_result;
        std::optional<Result<apdu::ActionResponse>> action_result;
        std::optional<Result<apdu::GetRecordResponse>> record_result;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { get_result = std::move(result); });
        peers.client->async_set({{model::Oad{0x2000, 2, 0}, model::Data{model::UInt16{1}}}}, false,
                                [&](auto result) { set_result = std::move(result); });
        peers.client->async_action({{model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{1}}}},
                                   false, [&](auto result) { action_result = std::move(result); });
        const apdu::GetRecord record{{0x5004, 2, 0}, model::SelectAll{}, {}};
        peers.client->async_get_record({record}, false,
                                       [&](auto result) { record_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(get_result);
        REQUIRE(set_result);
        REQUIRE(action_result);
        REQUIRE(record_result);
        test::require_error(*get_result, ErrorCode::not_associated);
        test::require_error(*set_result, ErrorCode::not_associated);
        test::require_error(*action_result, ErrorCode::not_associated);
        test::require_error(*record_result, ErrorCode::not_associated);
    }

    SECTION("服务器角色不能发起客户机业务事务") {
        std::optional<Result<apdu::GetResponse>> rejected;
        peers.server->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { rejected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(rejected);
        test::require_error(*rejected, ErrorCode::not_associated);
    }

    SECTION("关闭后所有事务以 closed 结束") {
        std::optional<Result<apdu::ConnectResponse>> connected;
        peers.client->async_connect([&](auto result) { connected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(connected);
        peers.client->close();
        peers.executor->run_ready();
        std::optional<Result<apdu::GetResponse>> get_result;
        std::optional<Result<apdu::SetResponse>> set_result;
        std::optional<Result<apdu::ActionResponse>> action_result;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { get_result = std::move(result); });
        peers.client->async_set({{model::Oad{0x2000, 2, 0}, model::Data{model::UInt16{1}}}}, false,
                                [&](auto result) { set_result = std::move(result); });
        peers.client->async_action({{model::Omd{0x4000, 1, 0}, model::Data{model::UInt8{1}}}},
                                   false, [&](auto result) { action_result = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(get_result);
        REQUIRE(set_result);
        REQUIRE(action_result);
        test::require_error(*get_result, ErrorCode::closed);
        test::require_error(*set_result, ErrorCode::closed);
        test::require_error(*action_result, ErrorCode::closed);
    }
}

TEST_CASE("取消与超时都会关闭物理通道", "[session][timeout][cancel]") {
    SECTION("取消在途事务后通道不可再用") {
        LoneClient lone(/*preset=*/true);
        std::optional<Result<apdu::GetResponse>> response;
        lone.session->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        lone.executor->run_ready();
        CHECK_FALSE(response);
        lone.session->cancel();
        lone.executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::cancelled);
        check_state(*lone.session, State::closed);
        // 物理通道已关闭：对端写入被拒绝，线上无法再交换任何字节。
        std::optional<Result<void>> write;
        lone.far->async_write(Bytes{1, 2, 3}, [&](Result<void> result) { write = result; });
        lone.executor->run_ready();
        REQUIRE(write);
        test::require_error(*write, ErrorCode::closed);
    }

    SECTION("无响应的事务按请求超时结束并断开通道") {
        LoneClient lone(/*preset=*/true, /*timeout=*/50ms);
        std::optional<Result<apdu::GetResponse>> response;
        lone.session->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        lone.executor->run_ready();
        CHECK_FALSE(response);
        // 未到期限不超时。
        lone.executor->advance(49ms);
        CHECK_FALSE(response);
        lone.executor->advance(1ms);
        REQUIRE(response);
        test::require_error(*response, ErrorCode::timeout);
        CHECK(response->error().context == "session transaction timeout");
        // 超时后关闭物理通道，隔离线上没有 generation 字段的迟到响应。
        check_state(*lone.session, State::closed);
        std::optional<Result<void>> write;
        lone.far->async_write(Bytes{1, 2, 3}, [&](Result<void> result) { write = result; });
        lone.executor->run_ready();
        REQUIRE(write);
        test::require_error(*write, ErrorCode::closed);
    }

    SECTION("迟到响应无法被关闭后的会话匹配") {
        LoneClient lone(/*preset=*/true, /*timeout=*/30ms);
        std::optional<Result<apdu::GetResponse>> first;
        lone.session->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { first = std::move(result); });
        lone.executor->run_ready();
        lone.executor->advance(30ms);
        REQUIRE(first);
        test::require_error(*first, ErrorCode::timeout);

        // 即使对端补发一个格式完全正确的响应，通道已关闭，字节根本不上线。
        apdu::GetResponse late{0, false, {{{0x2000, 2, 0}, model::Data{model::UInt16{1}}}}, {}};
        auto bytes = apdu::encode_apdu(late);
        REQUIRE(static_cast<bool>(bytes));
        std::optional<Result<void>> write;
        lone.far->async_write(std::move(bytes).value(),
                              [&](Result<void> result) { write = result; });
        lone.executor->run_ready();
        REQUIRE(write);
        test::require_error(*write, ErrorCode::closed);

        // 后续事务一律以 closed 结束，不会匹配到这个迟到响应。
        std::optional<Result<apdu::GetResponse>> second;
        lone.session->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { second = std::move(result); });
        lone.executor->run_ready();
        REQUIRE(second);
        test::require_error(*second, ErrorCode::closed);
    }

    SECTION("close 幂等且结束未完成事务") {
        LoneClient lone(/*preset=*/true);
        std::optional<Result<apdu::GetResponse>> response;
        lone.session->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        lone.executor->run_ready();
        lone.session->close();
        CHECK_NOTHROW(lone.session->close());
        lone.executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::closed);
        check_state(*lone.session, State::closed);
    }
}

TEST_CASE("成功事务在同一连接连续多轮复用六位序号", "[session][piid]") {
    Peers peers;
    unsigned value = 0;
    peers.server->set_request_handler([&](const apdu::GetRequest& request) {
        apdu::GetResponse response;
        for (const auto& attribute : request.attributes)
            response.attributes.push_back(
                {attribute, model::Data{model::UInt16{static_cast<std::uint16_t>(++value)}}});
        return response;
    });
    peers.executor->run_ready();
    // 不推进虚拟时钟；每轮超过完整 PIID 空间，成功响应必须继续交给当前请求。
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned i = 0; i < 1000; ++i) {
            std::optional<Result<apdu::GetResponse>> response;
            peers.client->async_get({{0x2000, 2, 0}}, false,
                                    [&](auto result) { response = std::move(result); });
            peers.executor->run_ready();
            REQUIRE(response);
            REQUIRE(static_cast<bool>(*response));
            REQUIRE(std::get<model::UInt16>(
                        std::get<model::Data>(response->value().attributes[0].result).payload)
                        .value == round * 1000 + i + 1);
        }
        check_state(*peers.client, State::associated);
    }
    peers.client->close();
    peers.server->close();
    peers.executor->run_ready();
}

TEST_CASE("异常事务的序号隔离避免迟到响应串单", "[session][timeout]") {
    // 使用较长的隔离期，让已用序号在虚拟时间推进前保持不可用。
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);
    SessionOptions client_options, server_options;
    client_options.role = Role::client;
    server_options.role = Role::server;
    client_options.preset_association = server_options.preset_association = true;
    client_options.id_reuse_delay = 1000ms;
    client_options.request_timeout = 100ms;
    server_options.id_reuse_delay = 1000ms;
    server_options.request_timeout = 100ms;
    auto client = std::make_shared<Session>(channels.first, executor, client_options);
    auto server = std::make_shared<Session>(channels.second, executor, server_options);
    client->start();
    server->start();
    executor->run_ready();
    server->set_request_handler([](const apdu::GetRequest&) -> apdu::GetResponse {
        throw std::runtime_error("remote failure");
    });
    executor->run_ready();

    // PIID 只有六位，用满 64 个后必须等到隔离期结束才能继续。
    unsigned failed = 0, quarantined = 0;
    for (int i = 0; i < 66; ++i) {
        std::optional<Result<apdu::GetResponse>> response;
        client->async_get({{0x2000, 2, 0}}, false,
                          [&](auto result) { response = std::move(result); });
        executor->run_ready();
        REQUIRE(response);
        REQUIRE_FALSE(static_cast<bool>(*response));
        if (response->error().code == ErrorCode::resource_limit) {
            // 序号耗尽时明确返回资源限制，而不是复用仍在隔离期的标识。
            test::require_error(*response, ErrorCode::resource_limit);
            CHECK(response->error().context == "PIID quarantine");
            ++quarantined;
        } else {
            test::require_error(*response, ErrorCode::remote_error);
            ++failed;
        }
    }
    CHECK(failed == 64);
    CHECK(quarantined == 2);

    SECTION("隔离期结束后序号重新可用") {
        executor->advance(1000ms);
        std::optional<Result<apdu::GetResponse>> response;
        client->async_get({{0x2000, 2, 0}}, false,
                          [&](auto result) { response = std::move(result); });
        executor->run_ready();
        REQUIRE(response);
        test::require_error(*response, ErrorCode::remote_error);
    }

    SECTION("隔离期短于请求超时的配置被拒绝") {
        // 隔离期必须覆盖对端最大响应寿命，否则构造阶段就应失败。
        SessionOptions invalid;
        invalid.preset_association = true;
        invalid.request_timeout = 1s;
        invalid.id_reuse_delay = 999ms;
        CHECK_THROWS_AS(Session(channels.first, executor, invalid), std::invalid_argument);
    }
    client->close();
    server->close();
    executor->run_ready();
}

TEST_CASE("会话内执行环境判定用于避免同步等待自身", "[session][threading]") {
    Peers peers(/*preset=*/true);
    CHECK_FALSE(peers.client->in_executor_thread());
    CHECK_FALSE(peers.server->in_executor_thread());

    SECTION("执行器任务内为真，任务外为假") {
        bool inside = false;
        peers.executor->post([&] { inside = peers.client->in_executor_thread(); });
        peers.executor->run_ready();
        CHECK(inside);
        CHECK_FALSE(peers.client->in_executor_thread());
    }

    SECTION("服务器处理器在自己的执行环境内被同步调用") {
        bool inside = false;
        peers.server->set_request_handler([&](const apdu::GetRequest& request) {
            inside = peers.server->in_executor_thread();
            apdu::GetResponse response;
            for (const auto& attribute : request.attributes)
                response.attributes.push_back({attribute, model::Data{model::UInt16{1}}});
            return response;
        });
        peers.executor->run_ready();
        std::optional<Result<apdu::GetResponse>> response;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        // 处理器在执行器内运行，因此不得阻塞等待同一执行环境。
        CHECK(inside);
    }
}

TEST_CASE("诊断回调报告损坏帧与不匹配帧", "[session][diagnostic]") {
    Peers peers(/*preset=*/true);

    SECTION("地址不匹配被报告且不影响关联") {
        // 服务器地址与会话配置不一致。
        peers.inject(
            peers.server_channel,
            peers.user_frame(0xC3, {link_layer::AddressType::single, 0, {0x99}}, {1, 2, 3}));
        const auto* mismatch = peers.diagnostic(ErrorCode::address_mismatch, "session SA/CA");
        REQUIRE(mismatch);
        CHECK(mismatch->offset == 0);
        // 诊断不影响会话状态，后续业务仍然可用。
        check_state(*peers.client, State::associated);
        peers.serve_values(model::Data{model::UInt16{1}});
        std::optional<Result<apdu::GetResponse>> response;
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
    }

    SECTION("方向不匹配被单独报告") {
        // 客户机只接受服务器方向（控制字最高位置位）的帧。
        peers.inject(peers.server_channel, peers.user_frame(0x43, {}, {1, 2, 3}));
        const auto* mismatch = peers.diagnostic(ErrorCode::direction_mismatch, "session DIR");
        REQUIRE(mismatch);
        CHECK(mismatch->offset == 3);
        check_state(*peers.client, State::associated);
    }

    SECTION("帧校验和错误按覆盖范围分类") {
        // 破坏 FCS 覆盖范围内的最后一个字节。
        Bytes corrupt = peers.user_frame(0xC3, {}, {1, 2, 3, 4, 5});
        corrupt[corrupt.size() - 2] ^= 0x01;
        peers.inject(peers.server_channel, corrupt);
        const auto* frame_error = peers.diagnostic(ErrorCode::checksum_frame, "FCS");
        REQUIRE(frame_error);
        check_state(*peers.client, State::associated);
    }

    SECTION("头部校验和错误被单独分类") {
        // 地址字节落在 HCS 覆盖范围内，且不会先触发地址长度检查。
        Bytes corrupt = peers.user_frame(0xC3, {}, {1, 2, 3, 4, 5});
        corrupt[5] ^= 0x01;
        peers.inject(peers.server_channel, corrupt);
        const auto* header_error = peers.diagnostic(ErrorCode::checksum_header, "stream HCS");
        REQUIRE(header_error);
        // 头部错误与帧校验错误是不同分类，不会混淆。
        CHECK(peers.diagnostic(ErrorCode::checksum_frame, "FCS") == nullptr);
    }

    SECTION("已关联时收到 RELEASE 通知会解除关联") {
        // 通知属于已建立的连接，因此结束关联而不是拒绝；原因是通知本身而不是诊断。
        apdu::ReleaseNotification notice{
            0, {{0x07, 0xe0, 1, 1, 0, 0, 0}}, {{0x07, 0xe0, 1, 1, 0, 0, 0}}, {}};
        auto bytes = apdu::encode_apdu(notice);
        REQUIRE(static_cast<bool>(bytes));
        // 释放通知由服务器发出且不是客户机发起，因此方向位为服务器方向。
        peers.inject(peers.server_channel, peers.user_frame(0x83, {}, std::move(bytes).value()));
        check_state(*peers.client, State::preconnected);
        // 通知不会抢占事务，因此不产生诊断。
        CHECK(peers.diagnostics.empty());
    }

    SECTION("未关联时收到 RELEASE 通知被报告为迟到") {
        Peers waiting(/*preset=*/false);
        apdu::ReleaseNotification notice{
            0, {{0x07, 0xe0, 1, 1, 0, 0, 0}}, {{0x07, 0xe0, 1, 1, 0, 0, 0}}, {}};
        auto bytes = apdu::encode_apdu(notice);
        REQUIRE(static_cast<bool>(bytes));
        waiting.inject(waiting.server_channel,
                       waiting.user_frame(0x83, {}, std::move(bytes).value()));
        const auto* reported =
            waiting.diagnostic(ErrorCode::not_associated, "unexpected release notification");
        REQUIRE(reported);
        // 状态保持预连接，通知没有改变会话。
        check_state(*waiting.client, State::preconnected);
    }

    SECTION("未注册诊断处理器时诊断被安全丢弃") {
        // 直接构造会话，不注册诊断处理器，验证缺省行为不会崩溃。
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = true;
        auto client = std::make_shared<Session>(channels.first, executor, options);
        client->start();
        executor->run_ready();
        auto frame = link_layer::encode_frame(
            link_layer::Frame{0xC3, {link_layer::AddressType::single, 0, {0x99}}, 0, {1, 2, 3}});
        REQUIRE(static_cast<bool>(frame));
        channels.second->async_write(std::move(frame).value(), [](Result<void>) {});
        CHECK_NOTHROW(executor->run_ready());
        // 会话不受诊断影响，仍然可以正常关闭。
        CHECK(client->state() == State::associated);
        client->close();
        executor->run_ready();
    }
}

TEST_CASE("链路分帧重试与重组超时", "[session][fragment]") {
    SECTION("对端不确认时按配置重发并最终以超时结束") {
        // 客户机单独存在：对端没有会话，永远不会发确认帧。
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = true;
        options.parameters.send_frame_bytes = 40;
        options.parameters.receive_frame_bytes = 40;
        // 构造时一致性位被掩码到允许取值，置位第三字节打开链路分帧能力。
        options.parameters.protocol[2] = 0x08;
        options.fragment_timeout = 10ms;
        options.fragment_retries = 1;
        options.request_timeout = 5000ms;
        options.id_reuse_delay = 5000ms;
        auto client = std::make_shared<Session>(channels.first, executor, options);
        client->start();
        executor->run_ready();

        // 有上限地读取对端收到的数据，避免通道关闭后无限投递。
        std::vector<Bytes> received;
        int attempts = 0;
        std::function<void()> pump = [&] {
            if (++attempts > 8) return;
            channels.second->async_read([&](Result<Bytes> result) {
                if (result) received.push_back(result.value());
                pump();
            });
        };
        pump();

        std::optional<Result<apdu::GetResponse>> response;
        std::vector<model::Oad> attributes;
        for (int i = 0; i < 8; ++i) attributes.push_back(model::Oad{0x2000, 2, 0});
        client->async_get(attributes, true, [&](auto result) { response = std::move(result); });
        executor->run_ready();
        // 请求超出单帧容量，以分片形式发出首片并等待确认。
        REQUIRE(received.size() == 1);
        {
            link_layer::FrameStreamDecoder decoder;
            const auto events = decoder.feed(ByteView{received.front()});
            REQUIRE(events.size() == 1);
            REQUIRE(std::holds_alternative<link_layer::Frame>(events.front()));
            const auto& frame = std::get<link_layer::Frame>(events.front());
            // 分片标志置位，功能码仍是用户数据。
            CHECK((frame.control & 0x20) != 0);
            CHECK((frame.control & 7) == 3);
        }
        CHECK_FALSE(response);

        // 第一次超时重发同一片段，不重放应用请求。
        executor->advance(10ms);
        CHECK(received.size() == 2);
        CHECK_FALSE(response);
        // 超过重试上限后放弃，并以分片确认超时关闭会话。
        executor->advance(10ms);
        REQUIRE(response);
        test::require_error(*response, ErrorCode::timeout);
        CHECK(response->error().context == "link fragment ACK timeout");
        check_state(*client, State::closed);
        client->close();
        executor->run_ready();
    }

    SECTION("收到首片后没有后续时按重组超时结束") {
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions client_options, server_options;
        client_options.role = Role::client;
        server_options.role = Role::server;
        client_options.preset_association = server_options.preset_association = true;
        for (auto* config : {&client_options, &server_options}) {
            config->parameters.send_frame_bytes = 40;
            config->parameters.receive_frame_bytes = 40;
            config->parameters.protocol[2] = 0x08;
            config->reassembly_timeout = 50ms;
        }
        auto client = std::make_shared<Session>(channels.first, executor, client_options);
        auto server = std::make_shared<Session>(channels.second, executor, server_options);
        client->start();
        server->start();
        executor->run_ready();
        check_state(*server, State::associated);

        // 手工发送一个起始片段，之后不再补齐剩余分片。
        auto apdu_bytes = apdu::encode_apdu(apdu::GetRequest{7, false, {{0x2000, 2, 0}}, {}});
        REQUIRE(static_cast<bool>(apdu_bytes));
        auto fragment = link_layer::encode_fragment(link_layer::Fragment{
            link_layer::FragmentType::first, 0, std::move(apdu_bytes).value()});
        REQUIRE(static_cast<bool>(fragment));
        auto frame = link_layer::encode_frame(
            link_layer::Frame{0x40 | 0x20 | 0x03, {}, 0, std::move(fragment).value()});
        REQUIRE(static_cast<bool>(frame));
        channels.first->async_write(std::move(frame).value(), [](Result<void>) {});
        executor->run_ready();
        // 首片被接受并等待后续，会话保持关联。
        check_state(*server, State::associated);
        executor->advance(50ms);
        // 重组没有进展，按单调超时关闭会话。
        check_state(*server, State::closed);
        client->close();
        server->close();
        executor->run_ready();
    }

    SECTION("未协商分帧能力时超长帧被直接拒绝") {
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = true;
        options.parameters.send_frame_bytes = 40;
        options.parameters.receive_frame_bytes = 40;
        options.parameters.protocol[2] = 0x00;  // 关闭链路分帧能力。
        options.request_timeout = 1000ms;
        options.id_reuse_delay = 1000ms;
        auto client = std::make_shared<Session>(channels.first, executor, options);
        client->start();
        executor->run_ready();
        std::optional<Result<apdu::GetResponse>> response;
        std::vector<model::Oad> attributes;
        for (int i = 0; i < 8; ++i) attributes.push_back(model::Oad{0x2000, 2, 0});
        client->async_get(attributes, true, [&](auto result) { response = std::move(result); });
        executor->run_ready();
        REQUIRE(response);
        // 与其静默截断，不如明确告知无法协商链路分帧。
        test::require_error(*response, ErrorCode::resource_limit);
        CHECK(response->error().context == "APDU requires unnegotiated link fragmentation");
        client->close();
        executor->run_ready();
    }

    SECTION("协商了分帧能力时大响应被完整重组") {
        Peers peers(/*preset=*/true);
        std::optional<Result<apdu::GetResponse>> response;
        peers.server->set_request_handler([](const apdu::GetRequest& request) {
            apdu::GetResponse result;
            for (const auto& attribute : request.attributes) {
                model::Array values;
                for (int i = 0; i < 30; ++i) values.value.push_back(model::Data{model::UInt32{1}});
                result.attributes.push_back({attribute, model::Data{values}});
            }
            return result;
        });
        peers.executor->run_ready();
        peers.client->async_get({{0x2000, 2, 0}}, false,
                                [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        REQUIRE(std::holds_alternative<model::Data>(response->value().attributes[0].result));
        CHECK(std::get<model::Data>(response->value().attributes[0].result)
                  .as<model::Array>()
                  .value.size() == 30);
    }
}

TEST_CASE("登录、心跳与退出登录", "[session][link]") {
    SECTION("服务器登录成功后按配置周期自动心跳") {
        auto executor = std::make_shared<ManualExecutor>();
        auto channels = transport::MemoryChannel::pair(executor);
        SessionOptions client_options, server_options;
        client_options.role = Role::client;
        server_options.role = Role::server;
        client_options.preset_association = server_options.preset_association = true;
        server_options.heartbeat_seconds = 1;
        auto client = std::make_shared<Session>(channels.first, executor, client_options);
        auto server = std::make_shared<Session>(channels.second, executor, server_options);
        std::vector<Error> diagnostics;
        server->set_diagnostic_handler([&](const Error& error) { diagnostics.push_back(error); });
        client->start();
        server->start();
        executor->run_ready();
        check_state(*server, State::associated);

        // 推进到心跳周期：服务器发出心跳，客户机在会话内自动应答。
        executor->advance(1s);
        check_state(*server, State::associated);
        check_state(*client, State::associated);
        CHECK(diagnostics.empty());
        // 连续多个周期仍然稳定，说明心跳得到应答后重新计时。
        executor->advance(3s);
        check_state(*server, State::associated);
        check_state(*client, State::associated);
        CHECK(diagnostics.empty());
        client->close();
        server->close();
        executor->run_ready();
    }

    SECTION("零周期不启用自动心跳，会话在空闲期限内保持稳定") {
        Peers peers(/*preset=*/true);
        // 推进时间但不超过协商的空闲超时，会话不会因为缺少心跳而断开。
        peers.executor->advance(30s);
        check_state(*peers.client, State::associated);
        check_state(*peers.server, State::associated);
        CHECK(peers.diagnostics.empty());
    }

    SECTION("空闲超时后服务器发出释放通知并回到预连接") {
        Peers peers(/*preset=*/true);
        // 协商的空闲超时为 100 秒，越过后服务器主动释放关联。
        peers.executor->advance(99s);
        check_state(*peers.server, State::associated);
        peers.executor->advance(2s);
        check_state(*peers.server, State::preconnected);
        // 客户机收到释放通知后也解除关联，并给出明确原因。
        const auto* notified =
            peers.diagnostic(ErrorCode::not_associated, "unexpected release notification");
        REQUIRE(notified);
        check_state(*peers.client, State::preconnected);
    }

    SECTION("单次心跳由服务器显式发起") {
        Peers peers(/*preset=*/true);
        std::optional<Result<apdu::LinkResponse>> response;
        peers.server->async_link(apdu::LinkRequestType::heartbeat, 0,
                                 [&](auto result) { response = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(response);
        REQUIRE(static_cast<bool>(*response));
        // 低三位为零表示成功，最高位是时钟可信标志。
        CHECK((response->value().result & 0x07) == 0);
    }

    SECTION("退出登录后服务器回到预连接") {
        Peers peers(/*preset=*/false, /*client_first=*/true, /*linked=*/true);
        std::optional<Result<apdu::LinkResponse>> login;
        peers.server->async_link(apdu::LinkRequestType::login, 0,
                                 [&](auto result) { login = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(login);
        REQUIRE(static_cast<bool>(*login));
        std::optional<Result<apdu::ConnectResponse>> connected;
        peers.client->async_connect([&](auto result) { connected = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(connected);
        check_state(*peers.server, State::associated);

        std::optional<Result<apdu::LinkResponse>> logout;
        peers.server->async_link(apdu::LinkRequestType::logout, 0,
                                 [&](auto result) { logout = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(logout);
        // 关联状态下退出登录被明确拒绝：必须先释放应用连接。
        test::require_error(*logout, ErrorCode::invalid_value);
        CHECK(logout->error().context == "LINK role/state");
        check_state(*peers.server, State::associated);

        // 先释放应用连接，再退出登录，服务器回到 disconnected。
        std::optional<Result<void>> released;
        peers.client->async_release([&](auto result) { released = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(released);
        CHECK(static_cast<bool>(*released));
        check_state(*peers.server, State::preconnected);
        peers.server->async_link(apdu::LinkRequestType::logout, 0,
                                 [&](auto result) { logout = std::move(result); });
        peers.executor->run_ready();
        REQUIRE(logout);
        REQUIRE(static_cast<bool>(*logout));
        check_state(*peers.server, State::disconnected);
    }
}

TEST_CASE("会话构造校验配置", "[session][lifecycle]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);

    SECTION("空通道与空执行器被拒绝") {
        CHECK_THROWS_AS(Session(nullptr, executor), std::invalid_argument);
        CHECK_THROWS_AS(Session(channels.first, nullptr), std::invalid_argument);
    }

    SECTION("非单地址被拒绝") {
        SessionOptions options;
        options.server.type = link_layer::AddressType::broadcast;
        CHECK_THROWS_AS(Session(channels.first, executor, options), std::invalid_argument);
    }

    SECTION("非法能力与超时配置被拒绝") {
        SessionOptions options;
        options.preset_association = true;
        options.request_timeout = 10s;
        options.id_reuse_delay = 5s;  // 隔离期短于请求超时。
        CHECK_THROWS_AS(Session(channels.first, executor, options), std::invalid_argument);
        options.id_reuse_delay = 10s;
        options.fragment_retries = 17;  // 上限为 16 次。
        CHECK_THROWS_AS(Session(channels.first, executor, options), std::invalid_argument);
    }
}

TEST_CASE("析构时不保活排队任务", "[session][lifecycle]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto channels = transport::MemoryChannel::pair(executor);

    SECTION("已提交请求在析构后仍以 closed 结束") {
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = true;
        options.request_timeout = 5000ms;
        std::optional<Result<apdu::GetResponse>> response;
        {
            auto client = std::make_shared<Session>(channels.first, executor, options);
            client->start();
            executor->run_ready();
            client->async_get({{0x2000, 2, 0}}, false,
                              [&](Result<apdu::GetResponse> r) { response = std::move(r); });
            executor->run_ready();
            // 对端没有会话，响应不会到来。
            REQUIRE_FALSE(response);
        }
        executor->run_ready();
        // 已提交请求的回调仍被交付，事务以 closed 结束而不是悬挂。
        REQUIRE(response);
        test::require_error(*response, ErrorCode::closed);
    }

    SECTION("排队任务不保活会话，会话析构后不会复活") {
        SessionOptions options;
        options.role = Role::client;
        options.preset_association = true;
        options.request_timeout = 5000ms;
        // 完成容器必须比会话活得久：析构仍会交付 closed 回调。
        std::optional<Result<apdu::GetResponse>> pending;
        std::weak_ptr<Session> weak;
        {
            auto client = std::make_shared<Session>(channels.first, executor, options);
            client->start();
            executor->run_ready();
            client->async_get({{0x2000, 2, 0}}, false,
                              [&](Result<apdu::GetResponse> r) { pending = std::move(r); });
            executor->run_ready();
            REQUIRE_FALSE(pending);
            // 会话在事务在途时析构，排队任务不保活它。
            weak = client;
        }
        CHECK(weak.expired());
        // 驱动执行器只交付完成回调，不会让已析构的会话复活。
        CHECK_NOTHROW(executor->run_ready());
        REQUIRE(pending);
        test::require_error(*pending, ErrorCode::closed);
    }

    SECTION("临时会话析构不留下未定义行为") {
        std::weak_ptr<Session> weak;
        CHECK_NOTHROW([&] {
            auto temporary = std::make_shared<Session>(channels.first, executor, SessionOptions{});
            temporary->start();
            weak = temporary;
        }());
        CHECK(weak.expired());
        CHECK_NOTHROW(executor->run_ready());
    }
}
