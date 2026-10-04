#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <modbus.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#include <vector>

#include "net/collect/plugin_manager.h"
#include "net/protocol/dynamic_library.h"
#include "net/protocol/modbus_protocol.h"

using ems::net::collect::PluginManager;
using Json = nlohmann::json;
namespace fs = std::filesystem;

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
std::string alias_plugin = EMS_REAL_PLUGIN;

void close_socket(int socket) {
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket));
#else
    ::close(socket);
#endif
}

bool ready(int socket) {
    fd_set readers;
    FD_ZERO(&readers);
    FD_SET(socket, &readers);
    timeval timeout{0, 100000};
    return select(socket + 1, &readers, nullptr, nullptr, &timeout) > 0;
}

int socket_port(int socket) {
    sockaddr_in address{};
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    CHECK(getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    return ntohs(address.sin_port);
}

struct Simulator {
    modbus_t* context{modbus_new_tcp("127.0.0.1", 0)};
    modbus_mapping_t* mapping{modbus_mapping_new(1024, 1024, 1024, 1024)};
    int listener{-1}, port{0};
    std::atomic<bool> running{true};
    std::atomic<int> reads{0};
    std::thread worker;

    Simulator() {
        CHECK(context && mapping);
        modbus_set_indication_timeout(context, 0, 100000);
        modbus_set_byte_timeout(context, 0, 100000);
        listener = modbus_tcp_listen(context, 16);
        CHECK(listener >= 0);
        port = socket_port(listener);
        mapping->tab_registers[0] = 0x1234;
        mapping->tab_registers[1] = 0x5678;
        mapping->tab_registers[2] = 0xfffe;
        mapping->tab_registers[3] = 0x4148;
        mapping->tab_registers[4] = 0;
        mapping->tab_registers[5] = 0;
        mapping->tab_registers[6] = 0x4148;
        mapping->tab_registers[7] = 0x4841;
        mapping->tab_registers[8] = 0;
        mapping->tab_registers[9] = 0;
        mapping->tab_registers[10] = 0x4841;
        mapping->tab_registers[11] = 0x4029;
        mapping->tab_registers[12] = 0;
        mapping->tab_registers[13] = 0;
        mapping->tab_registers[14] = 0;
        mapping->tab_registers[15] = 0x00ff;
        mapping->tab_input_registers[1] = 123;
        mapping->tab_bits[1] = 1;
        mapping->tab_input_bits[1] = 1;
        worker = std::thread([this] {
            std::array<std::uint8_t, MODBUS_TCP_MAX_ADU_LENGTH> query{};
            while (running) {
                if (!ready(listener)) continue;
                int socket = modbus_tcp_accept(context, &listener);
                if (socket < 0) continue;
                while (running) {
                    if (!ready(socket)) continue;
                    int size = modbus_receive(context, query.data());
                    if (size <= 0) break;
                    if (query[7] <= 4) ++reads;
                    modbus_reply(context, query.data(), size, mapping);
                }
                modbus_close(context);
                modbus_set_socket(context, -1);
            }
        });
    }

    ~Simulator() {
        running = false;
        if (worker.joinable()) worker.join();
        close_socket(listener);
        modbus_free(context);
        modbus_mapping_free(mapping);
    }
};

void configure(PluginManager& manager, const char* file) {
    const auto path = fs::u8path(file);
    manager.set_plugin_dir(path.parent_path().u8string());
    manager.register_mapping("test", path.filename().u8string());
}

struct Session {
    PluginManager& manager;
    void* instance;
    PluginManager::PluginFunctions functions;

    Session(PluginManager& owner, const Json& config) : manager(owner) {
        instance = manager.load("test");
        CHECK(instance);
        functions = manager.get_instance_functions(instance);
        CHECK(functions);
        const auto text = config.dump();
        CHECK(functions.init_fn(instance, text.c_str()) == 0);
        CHECK(functions.connect_fn(instance) == 0);
    }

    ~Session() {
        if (instance) manager.release(instance);
    }

    Json read(const std::vector<const char*>& codes, int expected) {
        std::array<char, 65536> output{};
        CHECK(functions.read_batch_fn(instance, codes.data(), static_cast<int>(codes.size()),
                                      output.data(), static_cast<int>(output.size())) == expected);
        return Json::parse(output.data());
    }

    int write(const char* code, double value) { return functions.write_fn(instance, code, value); }
};

Json client_config(int port) {
    return {{"host", "127.0.0.1"}, {"port", port}, {"timeout_ms", 200}};
}

void lazy_load() {
    PluginManager manager;
    configure(manager, EMS_NORMAL_PLUGIN);
    CHECK(manager.loaded_library_count() == 0);
    CHECK(!manager.is_loaded("test"));
    CHECK(!manager.load("unsupported"));
    CHECK(manager.loaded_library_count() == 0);
    void* instance = manager.load("test");
    CHECK(instance);
    CHECK(manager.loaded_library_count() == 1);
    CHECK(manager.release(instance));
    CHECK(!manager.release(instance));
    CHECK(manager.loaded_library_count() == 0);
}

void aliases() {
    PluginManager manager;
    const auto path = fs::u8path(alias_plugin);
    manager.set_plugin_dir(path.parent_path().u8string());
    void* tcp = manager.load("ModbusTCP");
    void* rtu = manager.load("ModbusRTU");
    CHECK(tcp && rtu && tcp != rtu);
    CHECK(manager.loaded_library_count() == 1);
    CHECK(manager.get_plugin_info(tcp));
    CHECK(manager.release(tcp));
    CHECK(manager.is_loaded("ModbusRTU"));
    const auto functions = manager.get_functions("ModbusRTU");
    CHECK(functions.is_connected_fn(rtu) == 0);
    CHECK(manager.release(rtu));
    CHECK(manager.loaded_library_count() == 0);

    // 通过 UTF-8 API 构造中文路径，不依赖 Windows 窄字符 argv 的本地代码页。
    const auto unicode = path.parent_path() / fs::u8path(u8"通讯协议 Unicode 测试");
    fs::create_directories(unicode);
    for (const auto& file : fs::directory_iterator(path.parent_path())) {
        if (file.is_regular_file() && file.path().extension() == ".dll")
            fs::copy_file(file.path(), unicode / file.path().filename(),
                          fs::copy_options::overwrite_existing);
    }
    if (path.extension() != ".dll")
        fs::copy_file(path, unicode / path.filename(), fs::copy_options::overwrite_existing);
    manager.set_plugin_dir(unicode.u8string());
    void* unicode_instance = manager.load("ModbusTCP");
    CHECK(unicode_instance);
    CHECK(manager.release(unicode_instance));
    CHECK(manager.loaded_library_count() == 0);
}

void concurrent_load() {
    PluginManager manager;
    configure(manager, EMS_NORMAL_PLUGIN);
    std::array<void*, 16> instances{};
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < instances.size(); ++i)
        workers.emplace_back([&, i] { instances[i] = manager.load("test"); });
    for (auto& worker : workers) worker.join();
    CHECK(manager.loaded_library_count() == 1);
    for (auto* instance : instances) {
        CHECK(instance);
        CHECK(manager.release(instance));
    }
    CHECK(manager.loaded_library_count() == 0);
}

void failures() {
    PluginManager manager;
    for (const auto* file : {EMS_BAD_ABI_PLUGIN, EMS_NULL_PLUGIN, EMS_MISSING_PLUGIN}) {
        configure(manager, file);
        CHECK(!manager.load("test"));
        CHECK(!manager.last_error().empty());
        CHECK(manager.loaded_library_count() == 0);
    }
    manager.register_mapping("test", "missing-file.dll");
    CHECK(!manager.load("test"));
    CHECK(manager.loaded_library_count() == 0);
    manager.register_mapping("test", "../outside.dll");
    CHECK(!manager.load("test"));
    CHECK(manager.last_error().find("outside") != std::string::npos);
    configure(manager, EMS_NORMAL_PLUGIN);
    void* instance = manager.load("test");
    CHECK(instance);
    CHECK(manager.release(instance));
}

void failed_stop() {
    PluginManager manager;
    configure(manager, EMS_NORMAL_PLUGIN);
    void* instance = manager.load("test");
    CHECK(instance);
    const auto functions = manager.get_instance_functions(instance);
    CHECK(functions.init_fn(instance, "fail_stop") == 0);
    CHECK(!manager.release(instance));
    CHECK(manager.loaded_library_count() == 1);
    CHECK(manager.get_instance_functions(instance));
    CHECK(functions.init_fn(instance, "recovered") == 0);
    CHECK(manager.release(instance));
    CHECK(manager.loaded_library_count() == 0);
}

void instance_routing() {
    PluginManager manager;
    configure(manager, EMS_NORMAL_PLUGIN);
    void* old_instance = manager.load("test");
    CHECK(old_instance);
    configure(manager, EMS_REAL_PLUGIN);
    void* new_instance = manager.load("test");
    CHECK(new_instance);
    CHECK(manager.loaded_library_count() == 2);
    CHECK(!manager.get_functions(std::string("test")));
    CHECK(manager.get_instance_functions(old_instance).is_connected_fn(old_instance) == 1);
    CHECK(manager.get_instance_functions(new_instance).is_connected_fn(new_instance) == 0);
    CHECK(manager.release(old_instance));
    CHECK(manager.get_instance_functions(new_instance));
    CHECK(manager.release(new_instance));
    CHECK(manager.loaded_library_count() == 0);
}

void client_types() {
    Simulator simulator;
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Session session(manager, client_config(simulator.port));
    const auto result = session.read(
        {"1:3:2:1", "1:3:0:2", "1:3:3:4:0", "1:3:5:4:1", "1:3:7:4:2", "1:3:9:4:3", "1:3:11:5",
         "1:3:15:6", "1:3:15:7", "1:1:1:8", "1:2:1:8", "1:4:1", "1:3:15:9:0:7"},
        13);
    CHECK(result[0]["value"] == -2);
    CHECK(result[1]["value"] == 0x12345678);
    for (int i = 2; i <= 6; ++i) CHECK(result[i]["value"] == 12.5);
    CHECK(result[7]["value"] == 255);
    CHECK(result[8]["value"] == -1);
    CHECK(result[9]["value"] == true);
    CHECK(result[10]["value"] == true);
    CHECK(result[11]["value"] == 123);
    CHECK(result[12]["value"] == true);
    for (const auto& point : result) {
        CHECK(point["quality"] == 0);
        CHECK(point["timestamp"] > 1000000000000LL);
    }
    const auto before = simulator.reads.load();
    std::array<char, 1024> output{};
    const char* bad[] = {"invalid"};
    CHECK(session.functions.read_batch_fn(session.instance, bad, 1, output.data(), output.size()) ==
          -1);
    CHECK(simulator.reads == before);
    const auto invalid = session.read({"1:3:2000"}, 0);
    CHECK(invalid[0]["quality"] != 0);
    CHECK(invalid[0]["value"].is_null());
}

void client_writes() {
    Simulator simulator;
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Session session(manager, client_config(simulator.port));
    CHECK(session.write("1:3:30:4:3", 12.25) == 0);
    CHECK(session.read({"1:3:30:4:3"}, 1)[0]["value"] == 12.25);
    CHECK(session.write("1:16:32:2:1", 4294967295.0) == 0);
    CHECK(session.read({"1:3:32:2:1"}, 1)[0]["value"] == 4294967295ULL);
    CHECK(session.write("1:6:34:1", -123) == 0);
    CHECK(session.read({"1:3:34:1"}, 1)[0]["value"] == -123);
    CHECK(session.write("1:5:20:8", 1) == 0);
    CHECK(session.read({"1:1:20:8"}, 1)[0]["value"] == true);
    CHECK(session.write("1:4:1", 1) == -1);
    CHECK(session.write("1:6:34:0", -1) == -1);
    CHECK(session.write("1:6:34:0", 65536) == -1);
    CHECK(session.write("1:6:34:0", 1.5) == -1);
    CHECK(session.write("1:5:20:8", 2) == -1);
    CHECK(session.write("1:16:65535:5", 1) == -1);
}

void server_readonly() {
    int port;
    {
        Simulator reservation;
        port = reservation.port;
    }
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Json config = client_config(port);
    config["local_role"] = "server";
    config["slaves"] = {1, 2};
    Session server(manager, config);
    CHECK(server.write("1:4:1:4:0", 12.5) == 0);
    CHECK(server.write("2:3:1:1", -12) == 0);
    Session first(manager, client_config(port));
    Session second(manager, client_config(port));
    CHECK(first.read({"1:4:1:4:0"}, 1)[0]["value"] == 12.5);
    CHECK(second.read({"2:3:1:1"}, 1)[0]["value"] == -12);
    CHECK(first.write("2:6:1:1", 7) == -1);
    CHECK(second.read({"2:3:1:1"}, 1)[0]["value"] == -12);
    CHECK(manager.loaded_library_count() == 1);
    CHECK(manager.release(first.instance));
    first.instance = nullptr;
    CHECK(second.read({"2:3:1:1"}, 1)[0]["value"] == -12);
}

void buffer_retry() {
    Simulator simulator;
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Session session(manager, client_config(simulator.port));
    const char* codes[] = {"1:3:0"};
    char tiny[2] = {'x', 'y'};
    CHECK(session.functions.read_batch_fn(session.instance, codes, 1, tiny, 1) ==
          EMS_PLUGIN_BUFFER_TOO_SMALL);
    CHECK(tiny[0] == '\0' && tiny[1] == 'y');
    CHECK(std::strstr(session.functions.last_error_fn(session.instance), "required="));
    const int before = simulator.reads;
    CHECK(session.read({"1:3:0"}, 1)[0]["value"] == 0x1234);
    CHECK(simulator.reads == before);
    CHECK(session.read({"1:3:0"}, 1)[0]["value"] == 0x1234);
    CHECK(simulator.reads == before + 1);
}

void legacy_config() {
    Simulator simulator;
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Json config = {{"protocol", "ModbusTCP"},
                   {"connection", "127.0.0.1:" + std::to_string(simulator.port)},
                   {"baud_rate", 0},
                   {"data_bits", 0},
                   {"stop_bits", 0},
                   {"parity", "none"},
                   {"settings", "{\"rtu_addr\":\"1\",\"timeout\":5}"}};
    Session session(manager, config);
    CHECK(session.read({"1:3:0"}, 1)[0]["value"] == 0x1234);
}

void batch_boundaries() {
    Simulator simulator;
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    Session session(manager, client_config(simulator.port));
    std::vector<std::string> storage;
    for (int i = 0; i < 124; ++i) storage.push_back("1:3:" + std::to_string(i));
    storage.push_back("1:3:124:2");
    storage.push_back("1:3:125");
    std::vector<const char*> codes;
    for (const auto& code : storage) codes.push_back(code.c_str());
    const auto result = session.read(codes, 126);
    CHECK(result.size() == 126);
    CHECK(simulator.reads == 2);
    CHECK(result[124]["value"] == 0);
    CHECK(result[125]["value"] == 0);
}

void rtu_config() {
    PluginManager manager;
    configure(manager, EMS_REAL_PLUGIN);
    void* instance = manager.load("test");
    CHECK(instance);
    const auto functions = manager.get_instance_functions(instance);
    CHECK(functions.init_fn(
              instance,
              "{\"protocol\":\"ModbusRTU\",\"connection\":\"EMS_NONEXISTENT_SERIAL_PORT\",\"baud_"
              "rate\":19200,\"data_bits\":7,\"stop_bits\":2,\"parity\":\"E\"}") == 0);
    CHECK(functions.connect_fn(instance) == -1);
    CHECK(!functions.is_connected_fn(instance));
    CHECK(std::strlen(functions.last_error_fn(instance)) > 0);
    CHECK(functions.init_fn(instance, "{\"transport\":\"unknown\"}") == -1);
    CHECK(functions.init_fn(instance, nullptr) == -1);
    CHECK(functions.init_fn(instance, "{") == -1);
    CHECK(functions.write_fn(nullptr, "1:6:0", 1) == -1);
    CHECK(manager.release(instance));
    CHECK(manager.loaded_library_count() == 0);
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string scenario = argc > 1 ? argv[1] : "";
        if (argc > 2) alias_plugin = argv[2];
        if (scenario == "lazy_load")
            lazy_load();
        else if (scenario == "aliases")
            aliases();
        else if (scenario == "concurrent_load")
            concurrent_load();
        else if (scenario == "failures")
            failures();
        else if (scenario == "failed_stop")
            failed_stop();
        else if (scenario == "instance_routing")
            instance_routing();
        else if (scenario == "client_types")
            client_types();
        else if (scenario == "client_writes")
            client_writes();
        else if (scenario == "server_readonly")
            server_readonly();
        else if (scenario == "buffer_retry")
            buffer_retry();
        else if (scenario == "rtu_config")
            rtu_config();
        else if (scenario == "legacy_config")
            legacy_config();
        else if (scenario == "batch_boundaries")
            batch_boundaries();
        else
            throw std::runtime_error("Unknown test scenario");
        std::cout << "PASS " << scenario << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
