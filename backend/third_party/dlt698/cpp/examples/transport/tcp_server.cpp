/**
 * @file tcp_server.cpp
 * @brief TCP 服务端示例：监听并接受一个客户机连接，用对象目录应答 GET/SET/ACTION。
 */
#include <chrono>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/tcp.hpp>
#include <thread>

#include "demo_device.hpp"

using namespace dlt698;
using namespace dlt698::service;
using namespace dlt698::session;
using namespace dlt698::transport;
using namespace std::chrono_literals;

namespace {

void usage() {
    std::cerr << "Usage: dlt698_tcp_server <bind-address> <port> [lifetime-seconds]\n"
                 "  Example: dlt698_tcp_server 0.0.0.0 4059 60\n";
}

/** 接受一个连接并在其上运行协议服务器，直到会话关闭、到达时限或收到 SIGINT。 */
void serve(std::shared_ptr<IoRuntime> runtime, std::shared_ptr<IExecutor> executor,
           std::shared_ptr<TcpChannel> channel, std::chrono::seconds lifetime) {
    SessionOptions options;
    // 电表地址（SA）默认 000000000000，六字节按线序填写，低有效字节在前。
    // 修改地址时须同步修改 tcp_client；例如 123456789012 对应 12 90 78 56 34 12。
    options.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    options.server.logical = 0;  // 逻辑地址，范围 0～3。
    options.client_address = 0;  // 客户端地址（CA），两端必须一致。
    // 协议角色由 role 决定，与谁拨号无关；服务端监听时也是 server 角色。
    options.role = Role::server;
    // 远程连接要求先由协议服务器发起 LINK 登录，并开启自动心跳。
    options.require_login = true;
    options.heartbeat_seconds = 5;
    auto session = std::make_shared<Session>(channel, executor, options);
    session->set_diagnostic_handler(
        [](const Error& e) { std::cerr << "Diagnostic: " << e.context << '\n'; });
    auto registry = demo::demo_registry();
    // ServerService 会把 GET/SET/ACTION 分发到目录，目录在会话存活期内必须有效。
    ServerService service(session, registry);
    session->start();
    session->async_link(protocol::apdu::LinkRequestType::login, 5, [](auto r) {
        std::cerr << "Login: " << (r ? "ok" : r.error().context) << '\n';
    });
    std::cout << "Session established, waiting up to " << lifetime.count() << "s\n" << std::flush;
    const auto deadline = IExecutor::Clock::now() + lifetime;
    // run_for 是事件循环的时间预算，不是单次请求超时；会话内部另有事务超时。
    while (!demo::interrupted && session->state() != State::closed &&
           IExecutor::Clock::now() < deadline)
        runtime->run_for(10ms);
    session->close();
    // 关闭投递是异步的，再驱动一次让 closed 回调真正执行。
    runtime->run_for(20ms);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        usage();
        return 2;
    }
    try {
        const auto port = static_cast<std::uint16_t>(demo::number(argv[2], 65535));
        const auto lifetime = argc == 4 ? std::chrono::seconds(demo::number(argv[3], 86400)) : 60s;
        demo::install_signal_handler();
        auto runtime = std::make_shared<IoRuntime>();
        auto executor = runtime->executor();
        // listen 同步绑定地址并返回监听器，端口为 0 时由系统分配。
        auto bound = TcpListener::listen(runtime, argv[1], port);
        if (!bound) throw std::runtime_error(bound.error().context);
        auto listener = std::move(bound).value();
        std::cout << "Listening " << argv[1] << ':' << listener->local_port() << std::endl;
        // 同一时刻只允许一个在途 accept；连接关闭后继续等待下一个客户机。
        // lifetime 是整个服务端的时限，各连接共享它，而不是每连接重新计时。
        const auto deadline = IExecutor::Clock::now() + lifetime;
        for (;;) {
            // accept 回调捕获弱状态：超时或 Ctrl+C 退出循环后回调仍可能被投递，
            // 捕获局部变量会悬垂，因此这里用共享状态延续其生命周期。
            struct AcceptState {
                std::shared_ptr<TcpChannel> channel;
                std::optional<Error> failure;
            };

            auto state = std::make_shared<AcceptState>();
            std::weak_ptr<AcceptState> weak = state;
            listener->async_accept([weak](auto result) {
                const auto accepted = weak.lock();
                if (!accepted) return;
                if (result)
                    accepted->channel = std::move(result).value();
                else
                    accepted->failure = result.error();
            });
            while (!state->channel && !state->failure && !demo::interrupted &&
                   IExecutor::Clock::now() < deadline)
                runtime->run_for(10ms);
            if (!state->channel) break;
            std::cout << "Accepted " << argv[1] << ':' << listener->local_port() << std::endl;
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                deadline - IExecutor::Clock::now());
            serve(runtime, executor, std::move(state->channel), remaining);
            if (demo::interrupted) break;
        }
        listener->close();
        runtime->run_for(20ms);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
