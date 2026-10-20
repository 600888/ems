#include <atomic>
#include <dlt698/app/server.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/tcp.hpp>
#include <filesystem>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#include "net/protocol/plugin_manager.h"

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
using Json = nlohmann::json;
using ems::net::collect::PluginManager;
using namespace std::chrono_literals;
namespace d = dlt698;

struct Plugin {
    PluginManager& manager;
    void* instance;
    PluginManager::PluginFunctions f;

    explicit Plugin(PluginManager& owner)
        : manager(owner),
          instance(owner.load("DLT698")),
          f(owner.get_instance_functions(instance)) {
        CHECK(instance);
    }

    ~Plugin() { manager.release(instance); }

    void init(const Json& config) {
        if (f.init_fn(instance, config.dump().c_str()) != 0)
            throw std::runtime_error(f.last_error_fn(instance));
    }

    void connect() {
        if (f.connect_fn(instance) != 0) throw std::runtime_error(f.last_error_fn(instance));
    }

    Json read(const std::vector<std::string>& codes, int expected) {
        std::vector<const char*> pointers;
        for (const auto& code : codes) pointers.push_back(code.c_str());
        std::vector<char> output(100000);
        CHECK(f.read_batch_fn(instance, pointers.data(), static_cast<int>(codes.size()),
                              output.data(), static_cast<int>(output.size())) == expected);
        return Json::parse(output.data());
    }
};

Json config(int port) { return {{"port", port}, {"ip", "127.0.0.1"}, {"timeout_ms", 1000}}; }

void lazy_load(PluginManager& manager) {
    CHECK(manager.loaded_library_count() == 0);
    {
        Plugin one(manager), two(manager);
        CHECK(one.instance != two.instance);
        CHECK(manager.loaded_library_count() == 1);
        CHECK(std::string(one.f.info_fn()->protocol_name) == "DLT698");
        CHECK(one.f.info_fn()->api_version == EMS_PLUGIN_API_VERSION);
        CHECK(one.f.is_connected_fn(one.instance) == 0);
        CHECK(one.f.connect_fn(one.instance) == -1);
    }
    CHECK(manager.loaded_library_count() == 0);
}

void invalid_config(PluginManager& manager) {
    Plugin plugin(manager);
    for (const auto& invalid :
         {Json::array(), Json{{"role", "bad"}}, Json{{"protocol", "DLT645"}},
          Json{{"transport", "udp"}}, Json{{"meter_address", "A"}}, Json{{"port", 0}},
          Json{{"port", 65536}}, Json{{"port", 1.5}}, Json{{"connection", "127.0.0.1:0"}},
          Json{{"timeout_ms", 0}}, Json{{"logical_address", 4}}, Json{{"client_address", 256}},
          Json{{"allow_writes", 1}}, Json{{"settings", "[]"}}, Json{{"points", Json::object()}},
          Json{{"points", Json::array({{{"oad", "F001/0/0"}}})}},
          Json{{"points", Json::array({{{"oad", "F001/2/0"}, {"type", "invalid"}}})}},
          Json{{"transport", "rtu"}},
          Json{{"transport", "rtu"}, {"serial_port", "COM1"}, {"parity", "invalid"}}}) {
        CHECK(plugin.f.init_fn(plugin.instance, invalid.dump().c_str()) == -1);
        CHECK(std::string(plugin.f.last_error_fn(plugin.instance)).size() > 0);
    }
    plugin.init(Json{{"connection", "COM9"},
                     {"rtu_addr", "1"},
                     {"timeout", 5},
                     {"baudrate", 9600},
                     {"databits", 8},
                     {"stopbits", 1},
                     {"parity", "E"}});
    CHECK(plugin.f.is_connected_fn(plugin.instance) == 0);
    CHECK(plugin.f.init_fn(plugin.instance, nullptr) == -1);
}

void client_read(PluginManager& manager) {
    auto device = std::make_shared<d::service::Device>();
    CHECK(device->define({0xF001,
                          "values",
                          {{2, d::model::DataType::int16},
                           {3, d::model::DataType::boolean},
                           {4, d::model::DataType::array},
                           {5, d::model::DataType::uint64}}}));
    CHECK(device->set({0xF001, 2, 0}, d::model::Int16{-123}));
    CHECK(device->set({0xF001, 3, 0}, d::model::Boolean{true}));
    CHECK(device->set({0xF001, 4, 0},
                      d::model::Array{{d::model::UInt16{2200}, d::model::UInt16{2300}}}));
    CHECK(device->set({0xF001, 5, 0}, d::model::UInt64{9007199254740992ULL}));
    d::app::ServerOptions options;
    options.protocol.server.bytes = {0, 0, 0, 0, 0, 0};
    d::app::Server server(device, options);
    CHECK(server.start_tcp("127.0.0.1", 0));
    Plugin plugin(manager);
    plugin.init(config(server.local_port()));
    plugin.connect();
    const auto values = plugin.read(
        {"F001/2/0", "F0010300", "0xF0010401", "F0010400", "F0010500", "F0020200", "invalid"}, 3);
    CHECK(values[0]["value"] == -123);
    CHECK(values[1]["value"] == 1);
    CHECK(values[2]["value"] == 2200);
    CHECK(values[3]["value"].is_null());
    CHECK(values[3]["data_hex"] == "01021208981208FC");
    CHECK(values[4]["value"].is_null());
    CHECK(values[4]["data_type"] == 21);
    CHECK(values[5]["dar"] == 4);
    std::vector<std::string> large(70, "F0010200");
    CHECK(plugin.read(large, 70).size() == 70);
    CHECK(plugin.read({}, 0).empty());
    CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    plugin.connect();
    CHECK(plugin.read({"F0010200"}, 1)[0]["value"] == -123);
    CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    CHECK(server.stop());
}

// 独立分层服务端允许 SET，验证客户端写入成功和类型/授权限制。
void client_write(PluginManager& manager) {
    auto runtime = std::make_shared<d::transport::IoRuntime>();
    const auto listening = d::transport::TcpListener::listen(runtime, "127.0.0.1", 0);
    CHECK(listening);
    auto listener = listening.value();
    auto executor = runtime->executor();
    std::shared_ptr<d::session::Session> session;
    auto object = std::make_shared<d::service::MemoryObject>();
    object->set(2, d::model::Int16{10});
    auto registry = std::make_shared<d::service::ObjectRegistry>();
    CHECK(registry->register_object({0xF001, "set", {{2, d::model::DataType::int16, true, true}}},
                                    object));
    listener->async_accept([&](auto result) {
        if (!result) return;
        d::session::SessionOptions options;
        options.role = d::session::Role::server;
        options.server.bytes = {0, 0, 0, 0, 0, 0};
        session = std::make_shared<d::session::Session>(result.value(), executor, options);
        d::service::ServerService service(session, registry);
        session->start();
    });
    std::thread worker([&] { runtime->run(); });
    auto stop = [&] {
        // 停止测试线程后再访问其拥有的 session，避免测试本身产生竞争。
        runtime->stop();
        worker.join();
        listener->close();
        if (session) session->close();
        runtime->restart();
        runtime->run_for(20ms);
    };
    try {
        Plugin plugin(manager);
        auto cfg = config(listener->local_port());
        cfg["connection_profile"] = "local_public";
        cfg["points"] = Json::array({{{"oad", "F0010200"}, {"type", "int16"}}});
        plugin.init(cfg);
        plugin.connect();
        CHECK(plugin.f.write_fn(plugin.instance, "F0010200", 12) == -1);
        CHECK(std::string(plugin.f.last_error_fn(plugin.instance)).find("allow_writes") !=
              std::string::npos);
        CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
        // 第二条连接由新一次接受接入，SET 不经过 EMS 只读服务端。
        listener->async_accept([&](auto result) {
            if (!result) return;
            d::session::SessionOptions options;
            options.role = d::session::Role::server;
            options.server.bytes = {0, 0, 0, 0, 0, 0};
            session = std::make_shared<d::session::Session>(result.value(), executor, options);
            d::service::ServerService service(session, registry);
            session->start();
        });
        cfg["allow_writes"] = true;
        plugin.init(cfg);
        plugin.connect();
        CHECK(plugin.f.write_fn(plugin.instance, "F0010200", -321) == 0);
        CHECK(plugin.read({"F0010200"}, 1)[0]["value"] == -321);
        CHECK(plugin.f.write_fn(plugin.instance, "F0010200", 1.5) == -1);
        CHECK(plugin.f.write_fn(plugin.instance, "F0010200", 32768) == -1);
        CHECK(plugin.f.write_fn(plugin.instance, "F0010200",
                                std::numeric_limits<double>::infinity()) == -1);
        CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    } catch (...) {
        stop();
        throw;
    }
    stop();
}

void server_buffer(PluginManager& manager) {
    // 用独立监听器取得空闲端口。
    auto runtime = std::make_shared<d::transport::IoRuntime>();
    auto probe = d::transport::TcpListener::listen(runtime, "127.0.0.1", 0).value();
    const auto port = probe->local_port();
    probe->close();
    runtime->run_for(10ms);
    Plugin plugin(manager);
    auto cfg = config(port);
    cfg["role"] = "server_readonly";
    cfg["points"] =
        Json::array({{{"code", "count"}, {"oad", "F0010200"}, {"type", "uint16"}, {"value", 42}}});
    plugin.init(cfg);
    CHECK(plugin.f.init_fn(plugin.instance, "{\"role\":\"bad\"}") == -1);
    plugin.connect();  // 无效配置没有覆盖旧配置。
    CHECK(plugin.f.init_fn(plugin.instance, cfg.dump().c_str()) == -1);
    const char* codes[] = {"count"};
    char tiny[2]{};
    CHECK(plugin.f.read_batch_fn(plugin.instance, codes, 1, tiny, 2) == -2);
    CHECK(tiny[0] == '\0');
    CHECK(plugin.f.read_batch_fn(plugin.instance, nullptr, 1, tiny, 2) == -1);
    CHECK(plugin.read({"count"}, 1)[0]["value"] == 42);
    CHECK(plugin.f.write_fn(plugin.instance, "count", 123) == 0);
    CHECK(plugin.read({"count"}, 1)[0]["value"] == 123);
    CHECK(plugin.f.write_fn(plugin.instance, "count", -1) == -1);
    CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    plugin.connect();
    CHECK(plugin.read({"count"}, 1)[0]["value"] == 123);
    CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
}

void connection_failures(PluginManager& manager) {
    auto runtime = std::make_shared<d::transport::IoRuntime>();
    // socket 可连接，但无 LINK 应答；验证登录超时和失败后的资源收尾。
    const auto passive = d::transport::TcpListener::listen(runtime, "127.0.0.1", 0).value();
    Plugin plugin(manager);
    auto cfg = config(passive->local_port());
    cfg["timeout_ms"] = 50;
    plugin.init(cfg);
    for (int i = 0; i < 2; ++i) {
        const auto before = std::chrono::steady_clock::now();
        CHECK(plugin.f.connect_fn(plugin.instance) == -1);
        CHECK(std::chrono::steady_clock::now() - before < 2s);
        CHECK(plugin.f.is_connected_fn(plugin.instance) == 0);
        CHECK(std::string(plugin.f.last_error_fn(plugin.instance)).find("timeout") !=
              std::string::npos);
        CHECK(plugin.f.disconnect_fn(plugin.instance) == 0);
    }
    passive->close();
    runtime->run_for(10ms);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        PluginManager manager;
        manager.set_plugin_dir(std::filesystem::path(EMS_DLT698_PLUGIN).parent_path().string());
        const std::string scenario = argv[1];
        if (scenario == "lazy_load")
            lazy_load(manager);
        else if (scenario == "invalid_config")
            invalid_config(manager);
        else if (scenario == "client_read")
            client_read(manager);
        else if (scenario == "client_write")
            client_write(manager);
        else if (scenario == "server_buffer")
            server_buffer(manager);
        else if (scenario == "connection_failures")
            connection_failures(manager);
        else if (scenario == "lifecycle") {
            for (int i = 0; i < 3; ++i) {
                server_buffer(manager);
                CHECK(manager.loaded_library_count() == 0);
                lazy_load(manager);
            }
        } else
            return 2;
        CHECK(manager.loaded_library_count() == 0);
        std::cout << scenario << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
