#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "net/handler/dlt645/dlt645_handler.h"
#include "net/handler/iec104/iec104_handler.h"
#include "net/handler/modbus/modbus_proto_handler.h"
#include "net/protocol/plugin_manager.h"
#ifdef EMS_CHANNEL_TESTS
#include "net/collect/channel_thread.h"
#endif

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
namespace h = ems::net::handler;
using ems::net::collect::PluginManager;
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

int unused_port() {
#ifdef _WIN32
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    auto endpoint = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(endpoint != INVALID_SOCKET);
    int size = sizeof(sockaddr_in);
#else
    const auto endpoint = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(endpoint >= 0);
    socklen_t size = sizeof(sockaddr_in);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(endpoint, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    CHECK(getsockname(endpoint, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    const int port = ntohs(address.sin_port);
#ifdef _WIN32
    closesocket(endpoint);
    WSACleanup();
#else
    close(endpoint);
#endif
    return port;
}

void init(h::ProtocolHandler& handler, const Json& config) {
    if (!handler.initialize(config)) throw std::runtime_error(handler.last_error());
}

void connect(h::ProtocolHandler& handler) {
    if (!handler.connect()) throw std::runtime_error(handler.last_error());
}

std::vector<h::PointValue> read(h::ProtocolHandler& handler, const std::vector<std::string>& codes,
                                int expected) {
    std::vector<h::PointValue> samples;
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    int count;
    do {
        count = handler.read_batch(codes, samples);
        if (count == expected) break;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    if (count != expected)
        throw std::runtime_error("read " + Json(codes).dump() + ": " + handler.last_error());
    CHECK(samples.size() == codes.size());
    for (std::size_t i = 0; i < codes.size(); ++i) CHECK(samples[i].code == codes[i]);
    return samples;
}

void factory() {
    PluginManager manager;
    auto modbus = h::create_handler(manager, "ModbusRTU");
    auto iec = h::create_handler(manager, "IEC104");
    auto dlt = h::create_handler(manager, "DLT645");
    CHECK(dynamic_cast<h::ModbusProtoHandler*>(modbus.get()));
    CHECK(dynamic_cast<h::Iec104Handler*>(iec.get()));
    CHECK(dynamic_cast<h::Dlt645Handler*>(dlt.get()));
    CHECK(manager.loaded_library_count() == 0);
    CHECK(!dlt->connect());
    CHECK(!dlt->last_error().empty());
    CHECK(!dlt->initialize(Json::array()));
    auto missing = h::create_handler(manager, "EMS_MISSING_PROTOCOL");
    CHECK(!missing->initialize(Json::object()));
    CHECK(manager.loaded_library_count() == 0);
    bool rejected = false;
    try {
        (void)h::create_handler(manager, "");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    std::vector<h::PointValue> values(1);
    CHECK(dlt->read_batch({"0x02010100"}, values) == -1);
    CHECK(values.empty());
}

#ifdef EMS_HANDLER_modbus_PLUGIN
void modbus() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_HANDLER_modbus_PLUGIN).parent_path().string());
    {
        h::ModbusProtoHandler server(manager), client(manager);
        const int port = unused_port();
        const Json cfg = {{"host", "127.0.0.1"}, {"port", port}, {"timeout_ms", 250}};
        auto server_cfg = cfg;
        server_cfg["role"] = "server_readonly";
        init(server, server_cfg);
        connect(server);
        CHECK(server.write_value("1:3:0:0", 1234));
        CHECK(server.write_value("1:1:0:8", 1));
        auto client_cfg = cfg;
        client_cfg.erase("timeout_ms");
        client_cfg["settings"] = Json{{"timeout_ms", 250}};
        init(client, client_cfg);
        connect(client);
        auto values = read(client, {"1:3:0:0", "1:1:0:8", "1:3:65535:0"}, 2);
        CHECK(values[0].value == 1234);
        CHECK(values[1].value == 1);
        CHECK(values[2].quality != 0);
        CHECK(values[0].timestamp > 0);
        // 原生库可能将异常响应标记为断连，使用处理器状态决定是否恢复会话。
        if (!client.is_connected()) {
            CHECK(client.disconnect());
            connect(client);
        }
        CHECK(!client.write_value("1:6:0:0", 100));
        if (!client.is_connected()) {
            CHECK(client.disconnect());
            connect(client);
        }
        CHECK(read(client, {"1:3:0:0"}, 1)[0].value == 1234);
        CHECK(!client.initialize(client_cfg));
        CHECK(client.disconnect());
        CHECK(!client.initialize(Json{{"port", 0}}));
        connect(client);  // 非法初始化不会破坏先前配置。
        CHECK(read(client, {"1:3:0:0"}, 1)[0].value == 1234);
        // 输出超过 ABI 初始缓冲区，适配器须完整扩容且保持输入顺序。
        const std::vector<std::string> large(1500, "1:3:0:0");
        values = read(client, large, static_cast<int>(large.size()));
        CHECK(values.back().value == 1234);
    }
    CHECK(manager.loaded_library_count() == 0);
}
#endif

#ifdef EMS_HANDLER_iec104_PLUGIN
void iec104() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_HANDLER_iec104_PLUGIN).parent_path().string());
    {
        auto server = h::create_handler(manager, "IEC104");
        auto client = h::create_handler(manager, "IEC104");
        Json cfg = {{"host", "127.0.0.1"},
                    {"port", unused_port()},
                    {"timeout_ms", 1000},
                    {"command_timeout_ms", 500},
                    {"common_addresses", {1}}};
        auto server_cfg = cfg;
        server_cfg["role"] = "server_readonly";
        server_cfg["points"] = Json::array(
            {{{"code", "1:100:13"}, {"value", 12.5}}, {{"code", "1:101:1"}, {"value", 1}}});
        init(*server, server_cfg);
        connect(*server);
        init(*client, cfg);
        connect(*client);
        auto values = read(*client, {"1:101:1", "1:100:13", "1:999:13"}, 2);
        CHECK(values[0].value == 1);
        CHECK(values[1].value == 12.5);
        CHECK(values[2].quality != 0);
        CHECK(values[1].details.contains("iec_quality"));
        CHECK(!client->write_value("1:100:45", 1));
        CHECK(server->write_value("1:100:13", 23.5));
        const auto deadline = std::chrono::steady_clock::now() + 4s;
        do {
            values = read(*client, {"1:100:13"}, 1);
            if (values[0].value == 23.5) break;
            std::this_thread::sleep_for(5ms);
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(values[0].value == 23.5);
        CHECK(client->disconnect());
        connect(*client);
        CHECK(read(*client, {"1:100:13"}, 1)[0].value == 23.5);
    }
    CHECK(manager.loaded_library_count() == 0);
}
#endif

#ifdef EMS_HANDLER_dlt645_PLUGIN
void dlt645() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_HANDLER_dlt645_PLUGIN).parent_path().string());
    {
        h::Dlt645Handler server(manager), client(manager);
        Json cfg = {{"ip", "127.0.0.1"},
                    {"port", unused_port()},
                    {"meter_address", "123456789012"},
                    {"timeout_ms", 250}};
        auto server_cfg = cfg;
        server_cfg["role"] = "server_readonly";
        server_cfg["points"] =
            Json::array({{{"di", "0x02010100"}, {"value", 220.1}},
                         {{"di", "0x01010000"}, {"value", 2.1234}, {"occur_time", 1767323040000LL}},
                         {{"di", "0x04000105"}, {"value", 1234}}});
        init(server, server_cfg);
        connect(server);
        cfg["allow_writes"] = true;
        init(client, cfg);
        connect(client);
        const auto values = read(client, {"0x01010000", "0x02010100", "0x04000105", "bad"}, 3);
        CHECK(std::abs(values[1].value - 220.1) < 0.02);
        CHECK(values[0].details.contains("occur_time"));
        CHECK(values[2].details["fields"][0]["value"] == "1234");
        CHECK(values[3].quality != 0);
        CHECK(!client.write_value("0x04000105", 2345));
        CHECK(server.write_value("0x02010100", 230.2));
        CHECK(read(client, {"0x02010100"}, 1)[0].value > 230);
        CHECK(client.disconnect());
        connect(client);
        CHECK(read(client, {"0x02010100"}, 1)[0].value > 230);
    }
    CHECK(manager.loaded_library_count() == 0);
}
#endif

#ifdef EMS_CHANNEL_TESTS
void channel(const std::string& protocol, const char* plugin_path,
             ems::model::ProtocolType protocol_type, const std::string& code, Json settings) {
    using namespace ems::net::collect;
    PluginManager manager;
    manager.set_plugin_dir(fs::path(plugin_path).parent_path().string());
    ems::rtdb::ChannelConfig config;
    config.id_ = 1;
    config.name_ = "handler-test-" + protocol;
    config.protocol_ = protocol_type;
    config.connection_ = "127.0.0.1:" + std::to_string(unused_port());
    settings["role"] = "server_readonly";
    settings["timeout_ms"] = 500;
    config.settings_ = settings.dump();
    ChannelThread worker(config, nullptr);
    worker.set_poll_interval(100);
    CHECK(worker.start(manager));
    auto command = worker.post_command(code, 23.5);
    CHECK(command.wait_for(5s) == std::future_status::ready);
    CHECK(command.get());
    {
        auto client = h::create_handler(manager, protocol);
        settings["role"] = "client";
        settings["connection"] = config.connection_;
        init(*client, settings);
        connect(*client);
        CHECK(std::abs(read(*client, {code}, 1)[0].value - 23.5) < 0.02);
    }
    worker.stop();
    worker.join();
    CHECK(worker.state() == ChannelState::Stopped);
    // 仍保留通道对象也必须释放插件实例，避免拖延库卸载。
    CHECK(manager.loaded_library_count() == 0);
    CHECK(worker.start(manager));
    worker.stop();  // 覆盖刚启动即停止，包括重连等待中的取消。
    worker.join();
    CHECK(worker.state() == ChannelState::Stopped);
    CHECK(manager.loaded_library_count() == 0);
}

void channel_failure() {
    using namespace ems::net::collect;
    PluginManager manager;
    manager.register_mapping("DLT645", "missing-channel-plugin.dll");
    ems::rtdb::ChannelConfig config;
    config.protocol_ = ems::model::ProtocolType::DLT645;
    config.name_ = "missing-handler-test";
    ChannelThread worker(config, nullptr);
    CHECK(!worker.start(manager));
    CHECK(worker.state() == ChannelState::Error);
    CHECK(!worker.last_error().empty());
    worker.stop();
    worker.join();
    CHECK(worker.state() == ChannelState::Stopped);
    CHECK(manager.loaded_library_count() == 0);
}
#endif
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const std::string scenario = argv[1];
        if (scenario == "factory") factory();
#ifdef EMS_HANDLER_modbus_PLUGIN
        else if (scenario == "modbus")
            modbus();
#endif
#ifdef EMS_HANDLER_iec104_PLUGIN
        else if (scenario == "iec104")
            iec104();
#endif
#ifdef EMS_HANDLER_dlt645_PLUGIN
        else if (scenario == "dlt645")
            dlt645();
#endif
#ifdef EMS_CHANNEL_TESTS
#ifdef EMS_HANDLER_modbus_PLUGIN
        else if (scenario == "channel_modbus")
            channel("ModbusTCP", EMS_HANDLER_modbus_PLUGIN, ems::model::ProtocolType::ModbusTCP,
                    "1:3:0:4", Json::object());
#endif
#ifdef EMS_HANDLER_iec104_PLUGIN
        else if (scenario == "channel_iec104")
            channel("IEC104", EMS_HANDLER_iec104_PLUGIN, ems::model::ProtocolType::IEC104,
                    "1:100:13",
                    Json{{"points", Json::array({{{"code", "1:100:13"}, {"value", 0}}})}});
#endif
#ifdef EMS_HANDLER_dlt645_PLUGIN
        else if (scenario == "channel_dlt645")
            channel("DLT645", EMS_HANDLER_dlt645_PLUGIN, ems::model::ProtocolType::DLT645,
                    "0x02010100",
                    Json{{"points", Json::array({{{"di", "0x02010100"}, {"value", 0}}})}});
#endif
        else if (scenario == "channel_failure")
            channel_failure();
#endif
        else
            return 2;
        std::cout << scenario << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
