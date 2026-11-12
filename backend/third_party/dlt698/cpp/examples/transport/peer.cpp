#include <atomic>
#include <csignal>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <iostream>

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol;
using namespace dlt698::session;
using namespace dlt698::service;
using namespace dlt698::transport;
using namespace std::chrono_literals;

namespace {
volatile std::sig_atomic_t interrupted = 0;

void stop(int) { interrupted = 1; }

unsigned number(const char* text, unsigned maximum, bool allow_zero = false) {
    const std::string input(text);
    std::size_t end = 0;
    const auto n = std::stoul(input, &end);
    if (end != input.size() || input.empty() || input.front() == '-' || (!n && !allow_zero) ||
        n > maximum)
        throw std::invalid_argument("numeric argument out of range");
    return static_cast<unsigned>(n);
}

void print_data(const Data& data) {
    auto bytes = codec::encode_data(data);
    if (!bytes) throw std::runtime_error(bytes.error().context);
    std::cout << "Data: " << to_hex(bytes.value()) << '\n';
}

// 同一源文件也编译为终端程序，该模式不调用客户机结果打印函数。
[[maybe_unused]] void print_result(const ObjectValue& value) {
    if (const auto dar = std::get_if<std::uint8_t>(&value))
        std::cout << "DAR=" << unsigned(*dar) << '\n';
    else
        print_data(std::get<Data>(value));
}

void usage() {
#ifdef DLT698_TERMINAL
    std::cerr << "Usage: dlt698_terminal tcp-listen|tcp-connect address port [lifetime-seconds]\n"
                 "       dlt698_terminal serial device baud [lifetime-seconds]\n";
#else
    std::cerr << "Usage: dlt698_master tcp-connect|tcp-listen address port get|set|action|record "
                 "[value]\n"
                 "       dlt698_master serial device baud get|set|action|record [value]\n";
#endif
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        usage();
        return 2;
    }
    try {
        const std::string mode = argv[1];
        if (mode != "tcp-connect" && mode != "tcp-listen" && mode != "serial") {
            usage();
            return 2;
        }
#ifdef DLT698_TERMINAL
        if (argc > 5) {
            usage();
            return 2;
        }
        const auto lifetime = argc == 5 ? number(argv[4], 86400) : 60;
#else
        if (argc < 5 || argc > 6) {
            usage();
            return 2;
        }
        const std::string command = argv[4];
        if (command != "get" && command != "set" && command != "action" && command != "record") {
            usage();
            return 2;
        }
        if ((command == "set" || command == "action") && argc != 6) {
            usage();
            return 2;
        }
        if ((command == "get" || command == "record") && argc != 5) {
            usage();
            return 2;
        }
        const auto argument = argc == 6 ? number(argv[5], 65535, true) : 1;
#endif
        std::signal(SIGINT, stop);
        auto runtime = std::make_shared<IoRuntime>();
        auto executor = runtime->executor();
        std::shared_ptr<IChannel> channel;
        std::shared_ptr<TcpChannel> dialing;
        std::shared_ptr<TcpListener> listener;
        std::optional<Error> connection_error;
        if (mode == "serial") {
            SerialOptions serial;
            serial.baud_rate = number(argv[3], 4000000);
            auto opened = SerialChannel::open(runtime, argv[2], serial);
            if (!opened) throw std::runtime_error(opened.error().context);
            SerialLinkOptions timing;
            timing.baud_rate = serial.baud_rate;
            timing.bits_per_character = 11;
            auto linked = SerialLinkChannel::wrap(opened.value(), executor, timing);
            channel = linked;
        } else {
            const auto port = static_cast<std::uint16_t>(number(argv[3], 65535));
            if (mode == "tcp-connect")
                dialing = TcpChannel::connect(runtime, argv[2], port, [&](auto result) {
                    if (result)
                        channel = dialing;
                    else
                        connection_error = result.error();
                });
            else {
                auto bound = TcpListener::listen(runtime, argv[2], port);
                if (!bound) throw std::runtime_error(bound.error().context);
                listener = bound.value();
                std::cout << "Listening " << argv[2] << ':' << listener->local_port() << std::endl;
                listener->async_accept([&](auto result) {
                    if (result)
                        channel = std::move(result).value();
                    else
                        connection_error = result.error();
                });
            }
            // 建连超时由示例显式管理；协议角色和 TCP 拨号方向分别配置。
            const auto deadline = IExecutor::Clock::now() + 10s;
            while (!channel && !connection_error && !interrupted &&
                   IExecutor::Clock::now() < deadline)
                runtime->run_for(10ms);
            if (!channel) {
                if (dialing) dialing->close();
                if (listener) listener->close();
                runtime->run_for(10ms);
                throw std::runtime_error(connection_error ? connection_error->context
                                                          : "connection timeout/interrupted");
            }
        }
        SessionOptions options;
        // 电表地址（SA）默认 000000000000；master 和 terminal 必须配置相同地址。
        // 六字节按线序填写，低有效字节在前；例如 123456789012 对应 12 90 78 56 34 12。
        options.server.bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        options.server.logical = 0;  // 逻辑地址，范围 0～3。
        options.client_address = 0;  // 客户端地址（CA），两端必须一致。
        options.require_login = mode != "serial";
#ifdef DLT698_TERMINAL
        options.role = Role::server;
        options.heartbeat_seconds = mode == "serial" ? 0 : 5;
#else
        options.role = Role::client;
        options.request_time_tag = Ti{0, 10};
#endif
        auto session = std::make_shared<Session>(channel, executor, options);
        session->set_diagnostic_handler(
            [](const Error& e) { std::cerr << "Diagnostic: " << e.context << '\n'; });
#ifdef DLT698_TERMINAL
        auto registry = std::make_shared<ObjectRegistry>();
        auto object = std::make_shared<MemoryObject>();
        object->set(2, UInt16{42});
        object->bind_method(
            1, [weak = std::weak_ptr<MemoryObject>(object)](const Omd&, const Data& p) {
                const auto object = weak.lock();
                if (!object) return ActionValue{2, {}};
                const auto value = p.as<UInt16>().value;
                object->set(2, UInt16{value});
                return ActionValue{0, Data{UInt16{value}}};
            });
        object->bind_record(3, [](const apdu::GetRecord& query) {
            // 示例记录来自固定模拟数据；复杂采集/表计选择器由应用后端实现。
            if (!std::holds_alternative<SelectAll>(query.rows))
                return apdu::RecordResult{query.attribute, query.columns, std::uint8_t{3}};
            Rcsd columns{Oad{0x2021, 2, 0}, Oad{0x2000, 2, 0}};
            if (!query.columns.empty() && !(query.columns == columns))
                return apdu::RecordResult{query.attribute, query.columns, std::uint8_t{3}};
            return apdu::RecordResult{
                query.attribute, std::move(columns),
                std::vector<apdu::RecordRow>{{DateTimeS{{7, 0xea, 10, 4, 0, 0, 0}}, UInt16{42}}}};
        });
        auto registered = registry->register_object(
            {0x2000,
             "模拟数据",
             {{2, DataType::uint16, true, true}, {3, DataType::null, true, false, true}},
             {{1, DataType::uint16, DataType::uint16, true}}},
            object);
        if (!registered) throw std::runtime_error(registered.error().context);
        ServerService service(session, registry);
        session->start();
        if (options.require_login)
            session->async_link(apdu::LinkRequestType::login, 5, [](auto r) {
                if (!r) std::cerr << r.error().context << '\n';
            });
        const auto deadline = IExecutor::Clock::now() + std::chrono::seconds(lifetime);
        while (!interrupted && session->state() != State::closed &&
               IExecutor::Clock::now() < deadline)
            runtime->run_for(10ms);
#else
        session->start();
        const auto deadline = IExecutor::Clock::now() + 10s;
        while (!interrupted && session->state() == State::disconnected &&
               IExecutor::Clock::now() < deadline)
            runtime->run_for(10ms);
        // start 也是投递任务，先处理一次事件，确保本地串口已进入预连接。
        runtime->run_for(10ms);
        if (session->state() != State::preconnected)
            throw std::runtime_error("login timeout/interrupted");
        SyncClientService client(session, [runtime](auto budget) { runtime->run_for(budget); });
        auto connected = client.connect();
        if (!connected || connected.value().result)
            throw std::runtime_error(connected ? "association rejected"
                                               : connected.error().context);
        if (command == "get") {
            auto r = client.get({0x2000, 2, 0});
            if (!r) throw std::runtime_error(r.error().context);
            print_result(r.value());
        } else if (command == "set") {
            auto r = client.set({0x2000, 2, 0}, UInt16{static_cast<std::uint16_t>(argument)});
            if (!r) throw std::runtime_error(r.error().context);
            std::cout << "DAR=" << unsigned(r.value()) << '\n';
        } else if (command == "action") {
            auto r = client.action({0x2000, 1, 0}, UInt16{static_cast<std::uint16_t>(argument)});
            if (!r) throw std::runtime_error(r.error().context);
            std::cout << "DAR=" << unsigned(r.value().dar) << '\n';
            if (r.value().data) print_data(*r.value().data);
        } else {
            auto r = client.get_record({{0x2000, 3, 0}, SelectAll{}, {}});
            if (!r) throw std::runtime_error(r.error().context);
            if (const auto dar = std::get_if<std::uint8_t>(&r.value().result))
                std::cout << "DAR=" << unsigned(*dar) << '\n';
            else
                for (const auto& row : std::get<std::vector<apdu::RecordRow>>(r.value().result))
                    for (const auto& d : row) print_data(d);
        }
        auto released = client.release();
        if (!released) throw std::runtime_error(released.error().context);
#endif
        session->close();
        if (listener) listener->close();
        runtime->run_for(20ms);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
