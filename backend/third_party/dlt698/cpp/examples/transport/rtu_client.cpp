/**
 * @file rtu_client.cpp
 * @brief RTU/串口客户端示例：打开串口，套上串行链路适配后完成一次命令。
 */
#include <chrono>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>

#include "demo_device.hpp"

using namespace dlt698;
using namespace dlt698::service;
using namespace dlt698::session;
using namespace dlt698::transport;
using namespace std::chrono_literals;

namespace {

void usage() {
    std::cerr << "Usage: dlt698_rtu_client <device> <baud> get|set|action|record [value]\n"
                 "  Device: COM4 (Windows) or /dev/ttyUSB1 (POSIX)\n"
                 "  Example: dlt698_rtu_client COM4 9600 get\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        usage();
        return 2;
    }
    try {
        const auto baud = demo::number(argv[2], 4000000);
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
        SerialOptions serial;
        serial.baud_rate = baud;
        auto opened = SerialChannel::open(runtime, argv[1], serial);
        if (!opened) throw std::runtime_error(opened.error().context);
        // 与服务端使用同一套串行链路参数：四个 FE 前导 + 至少 33 位收发间隔。
        SerialLinkOptions timing;
        timing.baud_rate = serial.baud_rate;
        timing.bits_per_character = 11;
        auto channel = SerialLinkChannel::wrap(std::move(opened).value(), executor, timing);
        SessionOptions options;
        // 目标电表地址（SA）默认 000000000000，须与 rtu_server 一致。
        // 六字节按线序填写，低有效字节在前；例如 123456789012 对应 12 90 78 56 34 12。
        options.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        options.server.logical = 0;  // 逻辑地址，范围 0～3。
        options.client_address = 0;  // 客户端地址（CA），两端必须一致。
        options.role = Role::client;
        options.request_time_tag = model::Ti{0, 10};
        auto session = std::make_shared<Session>(channel, executor, options);
        session->set_diagnostic_handler(
            [](const Error& e) { std::cerr << "Diagnostic: " << e.context << '\n'; });
        session->start();
        // 串口没有连接建立过程，预连接在 start 投递后立即生效，仍需驱动一次事件循环。
        runtime->run_for(10ms);
        if (session->state() != State::preconnected)
            throw std::runtime_error("preconnect timeout/interrupted");
        SyncClientService client(session, [runtime](auto budget) { runtime->run_for(budget); });
        auto connected = client.connect();
        if (!connected) throw std::runtime_error(connected.error().context);
        // CONNECT 被拒时外层 Result 仍然是成功的，必须检查 response.result。
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
