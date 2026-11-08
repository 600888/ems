#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cs104_slave.h>
#include <modbus.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#include "log/process_logger.hpp"
#include "net/collect/collect_engine.h"
#include "net/para_loader/cfg_loader.h"
#include "rtdb/backend/shared_memory_store.h"
#include "rtdb/client/realtime_client.h"
#include "rtdb/client/rtdb_manager.h"

using namespace ems;
using Json = nlohmann::json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
#define CHECK(value)                                                                      \
    do {                                                                                  \
        if (!(value)) throw std::runtime_error(std::to_string(__LINE__) + ": " + #value); \
    } while (false)

namespace {
std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

template <class Predicate>
void eventually(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(10ms);
    CHECK(predicate());
}

void close_socket(int socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

int port_of(int socket) {
    sockaddr_in address{};
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    CHECK(getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    return ntohs(address.sin_port);
}

bool readable(int socket, int wait_us = 0) {
    fd_set input;
    FD_ZERO(&input);
    FD_SET(socket, &input);
    timeval timeout{0, wait_us};
    return select(socket + 1, &input, nullptr, nullptr, &timeout) > 0;
}

struct Storage {
    const std::string name =
        "reuse_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path path = fs::temp_directory_path() / (name + ".json");
    std::unique_ptr<rtdb::RealtimeClient> owner;

    Storage() {
        std::ofstream(path) << Json{{"realtime",
                                     {{"backend",
                                       {{"type", "shm"},
                                        {"options",
                                         {{"segment_name", name}, {"segment_bytes", 1048576}}}}}}}}
                                   .dump();
        auto opened = rtdb::RealtimeClient::initialize(utf8(path), core::make_call_context(2s));
        CHECK(core::is_ok(opened));
        owner = std::get<0>(std::move(opened));
    }

    ~Storage() {
        owner.reset();
        rtdb::SharedMemoryStore::remove(name);
        std::error_code ignored;
        fs::remove(path, ignored);
    }
};

struct ModbusServer {
    modbus_t* context{modbus_new_tcp("127.0.0.1", 0)};
    std::array<modbus_mapping_t*, 3> maps{};
    std::array<std::atomic<int>, 3> reads{};
    std::atomic<int> accepted{0};
    std::atomic<bool> running{true};
    std::mutex mutex;
    int listener{-1}, port{0};
    std::thread worker;

    ModbusServer() {
        CHECK(context);
        modbus_set_indication_timeout(context, 0, 100000);
        modbus_set_byte_timeout(context, 0, 100000);
        listener = modbus_tcp_listen(context, 16);
        CHECK(listener >= 0);
        port = port_of(listener);
        for (int i = 1; i <= 2; ++i) {
            maps[i] = modbus_mapping_new(1, 1, 2, 1);
            CHECK(maps[i]);
            maps[i]->tab_registers[0] = i * 100;
        }
        worker = std::thread([this] {
            std::vector<int> peers;
            std::array<uint8_t, MODBUS_TCP_MAX_ADU_LENGTH> query{};
            while (running) {
                if (readable(listener, 10000)) {
                    const int peer = modbus_tcp_accept(context, &listener);
                    if (peer >= 0) {
                        peers.push_back(peer);
                        ++accepted;
                    }
                }
                for (auto it = peers.begin(); it != peers.end();) {
                    if (!readable(*it)) {
                        ++it;
                        continue;
                    }
                    modbus_set_socket(context, *it);
                    const int length = modbus_receive(context, query.data());
                    if (length <= 0) {
                        close_socket(*it);
                        it = peers.erase(it);
                        continue;
                    }
                    const int unit = query[6];
                    std::lock_guard lock(mutex);
                    if (unit >= 1 && unit <= 2) {
                        if (query[7] == 3) ++reads[unit];
                        modbus_reply(context, query.data(), length, maps[unit]);
                    } else
                        modbus_reply_exception(context, query.data(),
                                               MODBUS_EXCEPTION_GATEWAY_TARGET);
                    ++it;
                }
            }
            for (const int peer : peers) close_socket(peer);
        });
    }

    int value(int unit) {
        std::lock_guard lock(mutex);
        return maps[unit]->tab_registers[0];
    }

    ~ModbusServer() {
        running = false;
        worker.join();
        close_socket(listener);
        modbus_free(context);
        for (auto* map : maps)
            if (map) modbus_mapping_free(map);
    }
};

struct IecServer {
    int port{0};
    CS104_Slave server{nullptr};
    std::atomic<int> accepted{0};

    IecServer() {
        // Bind an ephemeral port separately; adjacent ports are not assumed available.
        const int socket_fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        port = port_of(socket_fd);
        close_socket(socket_fd);
        server = CS104_Slave_create(32, 32);
        CS104_Slave_setLocalAddress(server, "127.0.0.1");
        CS104_Slave_setLocalPort(server, port);
        CS104_Slave_setServerMode(server, CS104_MODE_CONNECTION_IS_REDUNDANCY_GROUP);
        CS104_Slave_setConnectionEventHandler(
            server,
            [](void* data, IMasterConnection, CS104_PeerConnectionEvent event) {
                if (event == CS104_CON_EVENT_CONNECTION_OPENED)
                    ++static_cast<IecServer*>(data)->accepted;
            },
            this);
        CS104_Slave_setInterrogationHandler(
            server,
            [](void* data, IMasterConnection peer, CS101_ASDU request, uint8_t) {
                auto* self = static_cast<IecServer*>(data);
                const int ca = CS101_ASDU_getCA(request);
                IMasterConnection_sendACT_CON(peer, request, false);
                auto response =
                    CS101_ASDU_create(CS104_Slave_getAppLayerParameters(self->server), false,
                                      CS101_COT_INTERROGATED_BY_STATION, 0, ca, false, false);
                auto value =
                    MeasuredValueShort_create(nullptr, 100, static_cast<float>(ca * 100), 0);
                CS101_ASDU_addInformationObject(response,
                                                reinterpret_cast<InformationObject>(value));
                IMasterConnection_sendASDU(peer, response);
                InformationObject_destroy(reinterpret_cast<InformationObject>(value));
                CS101_ASDU_destroy(response);
                IMasterConnection_sendACT_TERM(peer, request);
                return true;
            },
            this);
        CS104_Slave_start(server);
        CHECK(CS104_Slave_isRunning(server));
    }

    ~IecServer() { CS104_Slave_destroy(server); }
};

database::DeviceConfigRecords configuration(int port, bool iec = false) {
    database::DeviceConfigRecords rows;
    database::CfgDeviceRow station;
    station.id = 1;
    station.device_id = "station";
    station.node_kind = "station";
    rows.devices.push_back(station);
    for (int i = 1; i <= 2; ++i) {
        auto node = station;
        node.id = i + 1;
        node.device_id = "device-" + std::to_string(i);
        node.parent_device_id = i == 1 ? "station" : "device-1";
        node.node_kind = "device";
        rows.devices.push_back(node);
        database::CfgChannelRow channel;
        channel.id = 6 + i;
        channel.channel_id = "channel-" + std::to_string(i);
        channel.name = channel.channel_id;
        channel.protocol = iec ? "iec104" : "modbus";
        channel.conn_mode = "tcp_client";
        channel.endpoint = "127.0.0.1:" + std::to_string(port);
        channel.poll_interval_ms = i == 1 ? 100 : 500;
        channel.timeout_ms = 500;
        rows.channels.push_back(channel);
        database::CfgBindingRow binding;
        binding.id = i;
        binding.binding_id = "binding-" + std::to_string(i);
        binding.channel_id = channel.channel_id;
        binding.device_id = node.device_id;
        binding.device_address = std::to_string(i);
        binding.can_collect = binding.can_control = 1;
        rows.bindings.push_back(binding);
        database::CfgPointYcRow point;
        point.id = i;
        point.point_id = "point-" + std::to_string(i);
        point.owner_device_id = node.device_id;
        point.binding_id = binding.binding_id;
        point.wire_type = iec ? "f32" : "u16";
        point.address_space = "holding_register";
        point.register_count = 1;
        point.scale_factor = 2;
        point.offset_value = 1;
        if (iec) point.protocol_address_json = R"({"ioa":100,"type_id":13})";
        rows.points_yc.push_back(point);
        if (!iec) {
            database::CfgPointYtRow command;
            command.id = i;
            command.point_id = "command-" + std::to_string(i);
            command.owner_device_id = node.device_id;
            command.binding_id = binding.binding_id;
            command.wire_type = "u16";
            command.address_space = "holding_register";
            command.register_count = 1;
            command.scale_factor = 2;
            rows.points_yt.push_back(command);
        }
    }
    return rows;
}

void initialize(net::collect::CollectEngine& engine, Storage& storage) {
    net::collect::EngineConfig config;
    config.pluginDir_ = utf8(fs::path(EMS_REUSE_MODBUS_PLUGIN).parent_path());
    config.rtdbConfigPath_ = utf8(storage.path);
    config.notificationBackend_ = net::collect::NotificationBackend::None;
    CHECK(engine.initialize(config));
}

bool sample(net::collect::CollectEngine& engine, const char* id, double expected) {
    rtdb::YcPoint point;
    return engine.data_dispatcher()->rtdb_manager()->getRtDb()->getYc(id, point) &&
           point.quality_ == model::PointQuality::Good && point.value_ == expected;
}

void modbus(const std::string& scenario) {
    ModbusServer server, second;
    Storage storage;
    auto rows = configuration(server.port);
    if (scenario == "siblings") rows.devices[2].parent_device_id = "station";
    if (scenario == "unit" || scenario == "unit_boundary") {
        auto unit = rows.devices[0];
        unit.id = 10;
        unit.device_id = "storage-unit";
        unit.node_kind = "unit";
        unit.parent_device_id = scenario == "unit" ? "station" : "device-1";
        rows.devices.push_back(unit);
        rows.devices[2].parent_device_id = unit.device_id;
        if (scenario == "unit") rows.devices[1].parent_device_id = unit.device_id;
    }
    if (scenario == "settings") rows.channels[1].timeout_ms = 600;
    if (scenario == "endpoint")
        rows.channels[1].endpoint = "127.0.0.1:" + std::to_string(second.port);
    net::device::CfgLoader loader;
    CHECK(loader.configure(rows));
    net::collect::CollectEngine engine;
    initialize(engine, storage);
    CHECK(engine.start_all(loader) == 2);
    eventually([&] { return sample(engine, "point-1", 201) && sample(engine, "point-2", 401); });
    const bool shared = scenario == "modbus";
    CHECK(engine.connection_count() == (shared ? 1u : 2u));
    CHECK(engine.channel_count() == 2);
    CHECK((engine.find_channel(7) == engine.find_channel(8)) == shared);
    CHECK(server.accepted == (shared || scenario == "endpoint" ? 1 : 2));
    if (scenario == "endpoint") CHECK(second.accepted == 1);
    if (shared) {
        std::this_thread::sleep_for(1100ms);
        CHECK(server.reads[1] > server.reads[2] * 2);
        auto command = engine.find_channel(8)->post_command("command-2", 84);
        CHECK(command.wait_for(2s) == std::future_status::ready && command.get());
        CHECK(server.value(2) == 42 && server.value(1) == 100);
        CHECK(engine.stop_channel(7));
        std::this_thread::sleep_for(150ms);
        const auto stopped = server.reads[1].load();
        const auto active = server.reads[2].load();
        eventually([&] { return server.reads[2] > active; });
        CHECK(server.reads[1] == stopped);
        CHECK(engine.connection_count() == 1 && engine.find_channel(8)->is_running());
        auto extra = loader.channels()[0];
        extra.id_ = 9;
        CHECK(engine.start_channel(extra, loader.connection_scope(7)));
        CHECK(engine.connection_count() == 1);
        CHECK(engine.stop_channel(9));
        extra.id_ = 10;
        CHECK(engine.start_channel(extra));
        CHECK(engine.connection_count() == 2);
        CHECK(engine.find_channel(10) != engine.find_channel(8));
        CHECK(engine.stop_channel(10));
        CHECK(engine.stop_channel(8));
        CHECK(engine.connection_count() == 0);
        CHECK(engine.plugin_manager().loaded_library_count() == 0);
        CHECK(engine.start_all(loader) == 2);
        eventually([&] { return server.accepted == 2 && sample(engine, "point-2", 85); });
    }
    engine.stop_all();
    CHECK(engine.plugin_manager().loaded_library_count() == 0);
}

void iec104(bool siblings) {
    IecServer server;
    Storage storage;
    net::device::CfgLoader loader;
    auto rows = configuration(server.port, true);
    if (siblings) rows.devices[2].parent_device_id = "station";
    CHECK(loader.configure(rows));
    net::collect::CollectEngine engine;
    initialize(engine, storage);
    CHECK(engine.start_all(loader) == 2);
    eventually([&] { return sample(engine, "point-1", 201) && sample(engine, "point-2", 401); });
    CHECK(server.accepted == (siblings ? 2 : 1));
    CHECK(engine.connection_count() == (siblings ? 2u : 1u));
    const auto settings = Json::parse(engine.find_channel(7)->channel_config().settings_);
    CHECK(settings.at("common_addresses") == (siblings ? Json::array({1}) : Json::array({1, 2})));
    CHECK(engine.stop_channel(7));
    CHECK(engine.find_channel(8)->is_running());
    CHECK(server.accepted == (siblings ? 2 : 1));
    engine.stop_all();
    CHECK(engine.plugin_manager().loaded_library_count() == 0);
}
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA startup{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &startup) == 0);
#endif
    const ProcessLogger logger("test_connection_reuse");
    if (argc != 2) return 2;
    try {
        if (std::string(argv[1]) == "iec104" || std::string(argv[1]) == "iec104_siblings")
            iec104(std::string(argv[1]) == "iec104_siblings");
        else
            modbus(argv[1]);
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
