#include <dlt645/service/client_service.h>
#include <dlt645/service/server_service.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#include "net/protocol/plugin_manager.h"

using Json = nlohmann::json;
using ems::net::collect::PluginManager;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
constexpr const char* kMeter = "123456789012";
constexpr std::uint32_t kEnergy = 0x00010000;
constexpr std::uint32_t kDemand = 0x01010000;
constexpr std::uint32_t kVoltage = 0x02010100;
constexpr std::uint32_t kParameter = 0x04000105;

std::array<std::uint8_t, 6> address() { return {0x12, 0x90, 0x78, 0x56, 0x34, 0x12}; }

int unused_port() {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(io, {boost::asio::ip::tcp::v4(), 0});
    return acceptor.local_endpoint().port();
}

Json config(int port, bool server = false) {
    return {{"transport", "tcp"},      {"role", server ? "server_readonly" : "client"},
            {"ip", "127.0.0.1"},       {"port", port},
            {"meter_address", kMeter}, {"timeout_ms", 250}};
}

Json points() {
    return Json::array(
        {{{"code", "energy"}, {"di", kEnergy}, {"value", 123.45}},
         {{"code", "demand"}, {"di", kDemand}, {"value", 2.1234}, {"occur_time", 1767323040000LL}},
         {{"code", "voltage"}, {"di", kVoltage}, {"value", 220.1}},
         {{"code", "parameter"}, {"di", kParameter}, {"value", 1234}},
         {{"code", "events"},
          {"di", "0x03030000"},
          {"fields", {"000001,000002", "000003,000004", "000005,000006"}}}});
}

struct Plugin {
    PluginManager& manager;
    void* instance;
    PluginManager::PluginFunctions f;

    explicit Plugin(PluginManager& m)
        : manager(m), instance(m.load("DLT645")), f(m.get_instance_functions(instance)) {
        if (!instance) throw std::runtime_error(m.last_error());
    }

    ~Plugin() { manager.release(instance); }

    void init(const Json& value) {
        if (f.init_fn(instance, value.dump().c_str()) != 0)
            throw std::runtime_error(f.last_error_fn(instance));
    }

    void connect() {
        if (f.connect_fn(instance) != 0) throw std::runtime_error(f.last_error_fn(instance));
    }

    Json read(std::initializer_list<std::string> values, int expected) {
        std::vector<std::string> strings(values);
        std::vector<const char*> codes;
        for (const auto& code : strings) codes.push_back(code.c_str());
        std::vector<char> output(100000);
        CHECK(f.read_batch_fn(instance, codes.data(), static_cast<int>(codes.size()), output.data(),
                              static_cast<int>(output.size())) == expected);
        return Json::parse(output.data());
    }

    int write(const std::string& code, double value) {
        return f.write_fn(instance, code.c_str(), value);
    }
};

void lazy_load() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    CHECK(manager.loaded_library_count() == 0);
    {
        Plugin one(manager), two(manager);
        CHECK(one.instance != two.instance);
        CHECK(manager.loaded_library_count() == 1);
        CHECK(one.f.info_fn()->api_version == EMS_PLUGIN_API_VERSION);
        CHECK(std::string(one.f.info_fn()->protocol_name) == "DLT645");
        CHECK(one.f.is_connected_fn(one.instance) == 0);
    }
    CHECK(manager.loaded_library_count() == 0);
}

void invalid_config() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin plugin(manager);
    for (const auto& invalid :
         {Json::array(), Json{{"role", "invalid"}}, Json{{"transport", "udp"}},
          Json{{"meter_address", "123A"}}, Json{{"password", "123"}}, Json{{"port", 0}},
          Json{{"port", 65536}}, Json{{"port", 1.2}}, Json{{"timeout_ms", 0}},
          Json{{"timeout", 61}}, Json{{"bcd_endian", "invalid"}}, Json{{"allow_writes", 1}},
          Json{{"settings", "[]"}}, Json{{"points", Json::object()}}, Json{{"transport", "rtu"}},
          Json{{"transport", "rtu"}, {"serial_port", "COM99"}, {"parity", "bad"}}}) {
        CHECK(plugin.f.init_fn(plugin.instance, invalid.dump().c_str()) == -1);
        CHECK(std::string(plugin.f.last_error_fn(plugin.instance)).size() > 0);
        CHECK(plugin.f.is_connected_fn(plugin.instance) == 0);
    }
    auto valid = config(unused_port(), true);
    valid["points"] = points();
    plugin.init(valid);
    CHECK(plugin.f.init_fn(plugin.instance, "{bad") == -1);
    plugin.connect();  // 非法重配置不能破坏之前的配置。
    CHECK(plugin.f.init_fn(plugin.instance, valid.dump().c_str()) == -1);
    CHECK(plugin.read({"voltage"}, 1)[0]["quality"] == 0);
}

void client_classes() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), client(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    cfg = config(port);
    cfg["points"] = points();
    client.init(cfg);
    client.connect();
    auto result = client.read({"energy", "demand", "voltage", "parameter", "events"}, 4);
    CHECK(std::abs(result[0]["value"].get<double>() - 123.45) < 0.02);
    CHECK(std::abs(result[1]["value"].get<double>() - 2.1234) < 0.0002);
    CHECK(result[1].contains("occur_time"));
    CHECK(std::abs(result[2]["value"].get<double>() - 220.1) < 0.02);
    CHECK(result[3]["value"] == 1234);
    CHECK(result[4]["fields"].size() == 3);
    CHECK(result[4]["fields"][0]["value"] == "000001,000002");
    CHECK(result[4]["quality"] == 1);  // 成对事件不伪装为单值。
    CHECK(client.read({"0x0201FFFF", "bad", "0x05000000", "0x04000105:99"}, 0).size() == 4);
    CHECK(client.read({}, 0).empty());
}

void multi_meter() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), client(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] =
        Json::array({{{"di", kVoltage}, {"value", 220.1}},
                     {{"di", kVoltage}, {"value", 230.2}, {"meter_address", "000000000002"}}});
    server.init(cfg);
    server.connect();
    client.init(config(port));
    client.connect();
    const auto result = client.read({"123456789012:0x02010100", "000000000002:0x02010100"}, 2);
    CHECK(std::abs(result[0]["value"].get<double>() - 220.1) < 0.02);
    CHECK(std::abs(result[1]["value"].get<double>() - 230.2) < 0.02);
}

void client_writes() {
    const int port = unused_port();
    auto transport = std::make_shared<dlt645::transport::server::TcpServer>();
    dlt645::transport::server::TcpServerConfig endpoint;
    endpoint.ip = "127.0.0.1";
    endpoint.port = static_cast<std::uint16_t>(port);
    CHECK(transport->configure(endpoint));
    auto native = std::make_shared<dlt645::service::ServerService>(transport);
    native->init();
    native->setAddress(address());
    CHECK(native->set04(kParameter, {"1234"}));
    CHECK(native->start());
    CHECK(native->set04(0x04000103, {"15"}));
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    {
        Plugin client(manager);
        auto cfg = config(port);
        cfg["allow_writes"] = true;
        client.init(cfg);
        client.connect();
        CHECK(client.write("0x04000105", 2345) == 0);
        CHECK(client.read({"0x04000105"}, 1)[0]["value"] == 2345);
        CHECK(client.write("0x04000103", 30) == 0);
        CHECK(client.read({"0x04000103"}, 1)[0]["value"] == 30);
        CHECK(client.write("0x04000105", -1) == -1);
        CHECK(client.write("0x04000105", 10000) == -1);
        CHECK(client.write("0x04000105", 0.5) == -1);
        CHECK(client.write("0x04000105", std::numeric_limits<double>::infinity()) == -1);
        CHECK(client.write("0x02010100", 230) == -1);
        CHECK(client.write("0x04000102", 120000) == -1);
        CHECK(client.write("0x04000105:0", 1) == -1);
        CHECK(client.f.disconnect_fn(client.instance) == 0);
        cfg["password"] = "12345678";
        client.init(cfg);
        client.connect();
        CHECK(client.write("0x04000105", 1234) == -1);
        CHECK(client.f.disconnect_fn(client.instance) == 0);
        cfg["allow_writes"] = false;
        cfg.erase("password");
        client.init(cfg);
        client.connect();
        CHECK(client.write("0x04000105", 1234) == -1);
    }
    native->stop();
    // 第三方 service 持有 transport，transport 的处理器持有 service；测试清理该引用。
    transport->setConnectionHandler(nullptr);
    native.reset();
}

void server_readonly() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    CHECK(server.write("voltage", 230.2) == 0);
    CHECK(server.write("voltage", 230.25) == -1);
    CHECK(server.write("voltage", 800) == -1);
    CHECK(server.write("0x02010200", 200) == -1);
    CHECK(server.read({"voltage"}, 1)[0]["value"].get<double>() > 230);
    auto client = dlt645::service::ClientService::createTcpClient(
        "127.0.0.1", static_cast<std::uint16_t>(port), 250ms);
    client->setAddress(address());
    CHECK(client->connect());
    CHECK(!client->write04(kParameter, "2345"));
    const auto before = client->read04(kParameter);
    CHECK(before);
    CHECK(before->fields[0].value == "1234");
    auto replacement = address();
    replacement[0] = 0x99;
    CHECK(!client->writeAddress(replacement));
    CHECK(client->read02(kVoltage));
    client->disconnect();
}

void buffer_retry() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), client(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    client.init(config(port));
    client.connect();
    const char* code = "0x02010100";
    char tiny[2]{};
    CHECK(client.f.read_batch_fn(client.instance, &code, 1, tiny, 2) ==
          EMS_PLUGIN_BUFFER_TOO_SMALL);
    CHECK(tiny[0] == 0);
    CHECK(server.write("voltage", 230.2) == 0);
    CHECK(client.read({code}, 1)[0]["value"].get<double>() < 221);
    CHECK(client.read({code}, 1)[0]["value"].get<double>() > 230);
    CHECK(client.f.read_batch_fn(client.instance, nullptr, -1, tiny, 2) == -1);
    CHECK(client.f.read_batch_fn(client.instance, &code, 1, nullptr, 2) == -1);
    CHECK(client.f.read_batch_fn(client.instance, &code, 65537, tiny, 2) == -1);
    CHECK(client.f.read_batch_fn(client.instance, &code, 1, tiny, 2) == -2);
    CHECK(client.f.disconnect_fn(client.instance) == 0);
    CHECK(client.f.read_batch_fn(client.instance, &code, 1, tiny, 2) == -1);
}

void reconnect() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), client(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    client.init(config(port));
    client.connect();
    for (int i = 0; i < 3; ++i) {
        CHECK(client.read({"0x02010100"}, 1)[0]["quality"] == 0);
        CHECK(client.f.disconnect_fn(client.instance) == 0);
        CHECK(server.f.disconnect_fn(server.instance) == 0);
        CHECK(client.f.is_connected_fn(client.instance) == 0);
        server.connect();
        client.connect();
    }
}

// 与断开重连不同，这里完整卸载并重新载入 DLL，验证定义和通信资源的生命周期。
void reload_library() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    for (int i = 0; i < 3; ++i) {
        {
            Plugin server(manager), client(manager);
            const int port = unused_port();
            auto cfg = config(port, true);
            cfg["points"] = points();
            server.init(cfg);
            server.connect();
            client.init(config(port));
            client.connect();
            CHECK(client.read({"0x02010100"}, 1)[0]["quality"] == 0);
        }
        CHECK(manager.loaded_library_count() == 0);
    }
}

void legacy_rtu() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), client(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    client.init({{"connection", "127.0.0.1:" + std::to_string(port)},
                 {"settings", Json{{"rtu_addr", kMeter}, {"timeout", 1}}.dump()}});
    client.connect();
    CHECK(client.read({"0x02010100"}, 1)[0]["quality"] == 0);
    CHECK(client.f.disconnect_fn(client.instance) == 0);
#ifdef _WIN32
    const char* serial = "COM99999";
#else
    const char* serial = "/dev/ems-no-such-serial";
#endif
    client.init({{"connection", serial},
                 {"baud_rate", 9600},
                 {"data_bits", 8},
                 {"stop_bits", 1},
                 {"parity", "E"},
                 {"settings", Json{{"rtu_addr", "1"}, {"timeout", 1}}.dump()}});
    CHECK(client.f.connect_fn(client.instance) == -1);
    CHECK(client.f.is_connected_fn(client.instance) == 0);
}

void concurrent_instances() {
    PluginManager manager;
    manager.set_plugin_dir(fs::path(EMS_DLT645_PLUGIN).parent_path().string());
    Plugin server(manager), first(manager), second(manager);
    const int port = unused_port();
    auto cfg = config(port, true);
    cfg["points"] = points();
    server.init(cfg);
    server.connect();
    first.init(config(port));
    first.connect();
    second.init(config(port));
    second.connect();
    std::atomic<int> errors{0};
    auto run = [&](Plugin& plugin) {
        try {
            for (int i = 0; i < 10; ++i) CHECK(plugin.read({"0x02010100"}, 1)[0]["quality"] == 0);
        } catch (...) {
            ++errors;
        }
    };
    std::thread a([&] { run(first); }), b([&] { run(second); }), c([&] { run(first); });
    a.join();
    b.join();
    c.join();
    CHECK(errors == 0);
    CHECK(first.f.disconnect_fn(first.instance) == 0);
    CHECK(second.read({"0x02010100"}, 1)[0]["quality"] == 0);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const std::string scenario = argv[1];
        if (scenario == "lazy_load")
            lazy_load();
        else if (scenario == "invalid_config")
            invalid_config();
        else if (scenario == "client_classes")
            client_classes();
        else if (scenario == "multi_meter")
            multi_meter();
        else if (scenario == "client_writes")
            client_writes();
        else if (scenario == "server_readonly")
            server_readonly();
        else if (scenario == "buffer_retry")
            buffer_retry();
        else if (scenario == "reconnect") {
            reconnect();
            reload_library();
        } else if (scenario == "legacy_rtu")
            legacy_rtu();
        else if (scenario == "concurrent_instances")
            concurrent_instances();
        else
            return 2;
        std::cout << scenario << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
