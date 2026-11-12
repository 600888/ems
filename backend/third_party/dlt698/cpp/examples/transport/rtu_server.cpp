/**
 * @file rtu_server.cpp
 * @brief RTU/串口服务端示例：打开串口，套上串行链路适配后用对象目录应答请求。
 */
#include <chrono>
#include <dlt698/service/service.hpp>
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
    std::cerr << "Usage: dlt698_rtu_server <device> <baud> [lifetime-seconds]\n"
                 "  Device: COM3 (Windows) or /dev/ttyUSB0 (POSIX)\n"
                 "  Example: dlt698_rtu_server COM3 9600 60\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        usage();
        return 2;
    }
    try {
        const auto baud = demo::number(argv[2], 4000000);
        const auto lifetime = argc == 4 ? std::chrono::seconds(demo::number(argv[3], 86400)) : 60s;
        demo::install_signal_handler();
        auto runtime = std::make_shared<IoRuntime>();
        auto executor = runtime->executor();
        // SerialChannel 只负责原始字节的收发，不加 FE、不切 RS-485 方向。
        SerialOptions serial;
        serial.baud_rate = baud;
        auto opened = SerialChannel::open(runtime, argv[1], serial);
        if (!opened) throw std::runtime_error(opened.error().context);
        // SerialLinkChannel 适配 698 串行链路：每帧前加四个 FE，并保证至少 33 位收发间隔。
        // 这里没有传 async_drain，帧间隔只按字节数与波特率估算；手动方向切换的 RS-485
        // 必须由应用注入真实排空回调，否则无法保证硬件时序。
        SerialLinkOptions timing;
        timing.baud_rate = serial.baud_rate;
        // 11 位 = 起始 1 + 数据 8 + 校验 1 + 停止 1，须与实际串口字格式一致。
        timing.bits_per_character = 11;
        auto channel = SerialLinkChannel::wrap(std::move(opened).value(), executor, timing);
        SessionOptions options;
        // 电表地址（SA）默认 000000000000，六字节按线序填写，低有效字节在前。
        // 修改地址时须同步修改 rtu_client；例如 123456789012 对应 12 90 78 56 34 12。
        options.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        options.server.logical = 0;  // 逻辑地址，范围 0～3。
        options.client_address = 0;  // 客户端地址（CA），两端必须一致。
        options.role = Role::server;
        // 串口是本地直连，没有远程登录概念，因此不要求 LINK 登录，也不启用心跳。
        options.heartbeat_seconds = 0;
        auto session = std::make_shared<Session>(channel, executor, options);
        session->set_diagnostic_handler(
            [](const Error& e) { std::cerr << "Diagnostic: " << e.context << '\n'; });
        auto registry = demo::demo_registry();
        ServerService service(session, registry);
        session->start();
        std::cout << "Serving " << argv[1] << " at " << baud << " baud for up to "
                  << lifetime.count() << "s" << std::endl;
        const auto deadline = IExecutor::Clock::now() + lifetime;
        // RS-485 是半双工共享总线，串口上没有"连接"概念：一直等到时限或对端关闭。
        while (!demo::interrupted && session->state() != State::closed &&
               IExecutor::Clock::now() < deadline)
            runtime->run_for(10ms);
        session->close();
        runtime->run_for(20ms);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
