/**
 * @file tcp_client.cpp
 * @brief TCP 客户端示例：主动拨号，完成 CONNECT → 命令 → RELEASE 后退出。
 */
#include <chrono>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/tcp.hpp>

#include "demo_device.hpp"

using namespace dlt698;
using namespace dlt698::service;
using namespace dlt698::session;
using namespace dlt698::transport;
using namespace std::chrono_literals;

namespace {

void usage() {
    std::cerr << "Usage: dlt698_tcp_client <host> <port> get|set|action|record [value]\n"
                 "  Example: dlt698_tcp_client 127.0.0.1 4059 get\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        usage();
        return 2;
    }
    try {
        const auto port = static_cast<std::uint16_t>(demo::number(argv[2], 65535));
        const std::string command = argv[3];
        if (command != "get" && command != "set" && command != "action" && command != "record") {
            usage();
            return 2;
        }
        if ((command == "set" || command == "action") != (argc == 5)) {
            usage();
            return 2;
        }
        const auto argument = argc == 5 ? demo::number(argv[4], 65535, true) : 1;
        demo::install_signal_handler();
        auto runtime = std::make_shared<IoRuntime>();
        auto executor = runtime->executor();
        // 拨号是异步的，成功回调之前通道不能读写；这里由示例显式管理建连超时。
        std::shared_ptr<TcpChannel> channel;
        std::shared_ptr<TcpChannel> dialing;
        std::optional<Error> failure;
        dialing = TcpChannel::connect(runtime, argv[1], port, [&](auto result) {
            if (result)
                channel = dialing;
            else
                failure = result.error();
        });
        const auto dial_deadline = IExecutor::Clock::now() + 10s;
        while (!channel && !failure && !demo::interrupted &&
               IExecutor::Clock::now() < dial_deadline)
            runtime->run_for(10ms);
        if (!channel) {
            if (dialing) dialing->close();
            runtime->run_for(10ms);
            throw std::runtime_error(failure ? failure->context : "connect timeout/interrupted");
        }
        SessionOptions options;
        // 目标电表地址（SA）默认 000000000000，须与 tcp_server 一致。
        // 六字节按线序填写，低有效字节在前；例如 123456789012 对应 12 90 78 56 34 12。
        options.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        options.server.logical = 0;  // 逻辑地址，范围 0～3。
        options.client_address = 0;  // 客户端地址（CA），两端必须一致。
        // 协议角色与拨号方向独立：这里是主动拨号方，但角色仍是协议客户机。
        options.role = Role::client;
        options.request_time_tag = model::Ti{0, 10};
        auto session = std::make_shared<Session>(channel, executor, options);
        session->set_diagnostic_handler(
            [](const Error& e) { std::cerr << "Diagnostic: " << e.context << '\n'; });
        session->start();
        // start 是投递任务，先驱动一次让接收泵真正开始。
        runtime->run_for(10ms);
        if (session->state() != State::preconnected)
            throw std::runtime_error("preconnect timeout/interrupted");
        // SyncClientService 用 drive 回调在本线程推进事件循环，避免另建线程。
        SyncClientService client(session, [runtime](auto budget) { runtime->run_for(budget); });
        auto connected = client.connect();
        // 业务成功不能只看外层错误：Result 成功但 result 非零表示被服务端拒绝。
        if (!connected) throw std::runtime_error(connected.error().context);
        if (connected.value().result) {
            std::cerr << "Association rejected, result=" << unsigned(connected.value().result)
                      << '\n';
            session->close();
            runtime->run_for(20ms);
            return 1;
        }
        demo::run_command(client, command, argument);
        auto released = client.release();
        if (!released) throw std::runtime_error(released.error().context);
        session->close();
        runtime->run_for(20ms);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
