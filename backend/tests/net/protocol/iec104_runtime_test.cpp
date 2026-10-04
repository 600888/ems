#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cs104_connection.h>
#include <cs104_slave.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#include <tuple>

#include "net/protocol/iec104_protocol.h"
#include "net/protocol/plugin_manager.h"

using Json = nlohmann::json;
using ems::net::collect::PluginManager;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
std::string plugin_path = EMS_IEC104_PLUGIN;
constexpr std::int64_t kTimestamp = 1767225600123LL;
const std::vector<int> kMonitoring = {1, 2, 3, 4, 9, 10, 11, 12, 13, 14, 21, 30, 31, 34, 35, 36};
const std::vector<int> kCommands = {45, 46, 47, 48, 49, 50, 58, 59, 60, 61, 62, 63};

template <typename Predicate>
void eventually(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    do {
        if (predicate()) return;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(predicate());
}

int unused_port() {
#ifdef _WIN32
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const auto socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(socket_handle != INVALID_SOCKET);
    int length = sizeof(sockaddr_in);
#else
    const int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(socket_handle >= 0);
    socklen_t length = sizeof(sockaddr_in);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    CHECK(getsockname(socket_handle, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const int port = ntohs(address.sin_port);
#ifdef _WIN32
    closesocket(socket_handle);
    WSACleanup();
#else
    close(socket_handle);
#endif
    return port;
}

struct Plugin {
    PluginManager& manager;
    void* instance{nullptr};
    PluginManager::PluginFunctions functions;

    explicit Plugin(PluginManager& owner) : manager(owner) {
        instance = manager.load("IEC104");
        if (!instance) throw std::runtime_error(manager.last_error());
        functions = manager.get_instance_functions(instance);
    }

    ~Plugin() { manager.release(instance); }

    void init(const Json& config) {
        if (functions.init_fn(instance, config.dump().c_str())) throw std::runtime_error(error());
    }

    void connect() {
        if (functions.connect_fn(instance)) throw std::runtime_error(error());
    }

    std::string error() { return functions.last_error_fn(instance); }

    Json read(const std::vector<std::string>& codes) {
        std::vector<const char*> pointers;
        for (const auto& code : codes) pointers.push_back(code.c_str());
        std::vector<char> output(65536);
        CHECK(functions.read_batch_fn(instance, pointers.data(), static_cast<int>(codes.size()),
                                      output.data(), static_cast<int>(output.size())) >= 0);
        return Json::parse(output.data());
    }

    int write(const std::string& code, double value) {
        return functions.write_fn(instance, code.c_str(), value);
    }
};

void configure_manager(PluginManager& manager) {
    const auto path = fs::absolute(fs::u8path(plugin_path));
    manager.set_plugin_dir(path.parent_path().u8string());
    manager.register_mapping("IEC104", path.filename().u8string());
}

Json client_config(int port) {
    return {{"host", "127.0.0.1"},        {"port", port},
            {"common_addresses", {1, 2}}, {"timeout_ms", 1000},
            {"command_timeout_ms", 1000}, {"originator_address", 7}};
}

double monitoring_value(int type) {
    if (type == 1 || type == 2 || type == 30) return 1;
    if (type == 3 || type == 4 || type == 31) return 2;
    if (type == 9 || type == 10 || type == 21 || type == 34) return -0.5;
    if (type == 11 || type == 12 || type == 35) return -1234;
    return 12.5;
}

InformationObject telemetry(int type, int ioa, double value, QualityDescriptor quality = 0) {
    sCP56Time2a time56{};
    sCP24Time2a time24{};
    CP56Time2a_createFromMsTimestamp(&time56, kTimestamp);
    const int state = static_cast<int>(value);
    const float floating = static_cast<float>(value);
    switch (type) {
        case 1:
            return (InformationObject)SinglePointInformation_create(nullptr, ioa, state != 0,
                                                                    quality);
        case 2:
            return (InformationObject)SinglePointWithCP24Time2a_create(nullptr, ioa, state != 0,
                                                                       quality, &time24);
        case 3:
            return (InformationObject)DoublePointInformation_create(
                nullptr, ioa, (DoublePointValue)state, quality);
        case 4:
            return (InformationObject)DoublePointWithCP24Time2a_create(
                nullptr, ioa, (DoublePointValue)state, quality, &time24);
        case 9:
            return (InformationObject)MeasuredValueNormalized_create(nullptr, ioa, floating,
                                                                     quality);
        case 10:
            return (InformationObject)MeasuredValueNormalizedWithCP24Time2a_create(
                nullptr, ioa, floating, quality, &time24);
        case 11:
            return (InformationObject)MeasuredValueScaled_create(nullptr, ioa, state, quality);
        case 12:
            return (InformationObject)MeasuredValueScaledWithCP24Time2a_create(nullptr, ioa, state,
                                                                               quality, &time24);
        case 13:
            return (InformationObject)MeasuredValueShort_create(nullptr, ioa, floating, quality);
        case 14:
            return (InformationObject)MeasuredValueShortWithCP24Time2a_create(
                nullptr, ioa, floating, quality, &time24);
        case 21:
            return (InformationObject)MeasuredValueNormalizedWithoutQuality_create(nullptr, ioa,
                                                                                   floating);
        case 30:
            return (InformationObject)SinglePointWithCP56Time2a_create(nullptr, ioa, state != 0,
                                                                       quality, &time56);
        case 31:
            return (InformationObject)DoublePointWithCP56Time2a_create(
                nullptr, ioa, (DoublePointValue)state, quality, &time56);
        case 34:
            return (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
                nullptr, ioa, floating, quality, &time56);
        case 35:
            return (InformationObject)MeasuredValueScaledWithCP56Time2a_create(nullptr, ioa, state,
                                                                               quality, &time56);
        case 36:
            return (InformationObject)MeasuredValueShortWithCP56Time2a_create(
                nullptr, ioa, floating, quality, &time56);
        default:
            return nullptr;
    }
}

struct Simulator {
    CS104_Slave server{nullptr};
    int port{unused_port()};
    std::atomic<int> interrogations{0}, reads{0}, selections{0}, executions{0}, mode{0};
    std::atomic<double> last_command{0};

    Simulator() {
        server = CS104_Slave_create(1024, 1024);
        CHECK(server);
        CS104_Slave_setLocalAddress(server, "127.0.0.1");
        CS104_Slave_setLocalPort(server, port);
        CS104_Slave_setServerMode(server, CS104_MODE_CONNECTION_IS_REDUNDANCY_GROUP);
        CS104_Slave_setInterrogationHandler(server, gi, this);
        CS104_Slave_setASDUHandler(server, command, this);
        CS104_Slave_setReadHandler(server, read, this);
        CS104_Slave_start(server);
        CHECK(CS104_Slave_isRunning(server));
    }

    ~Simulator() { CS104_Slave_destroy(server); }

    bool send(IMasterConnection peer, int ca, int ioa, int type, double value,
              CS101_CauseOfTransmission cot, QualityDescriptor quality = 0, int oa = 0,
              bool invalid_time = false) {
        auto asdu = CS101_ASDU_create(CS104_Slave_getAppLayerParameters(server), false, cot, oa, ca,
                                      false, false);
        auto object = telemetry(type, ioa, value, quality);
        if (!asdu || !object) return false;
        if (invalid_time && type == 36)
            CP56Time2a_setMonth(MeasuredValueShortWithCP56Time2a_getTimestamp(
                                    (MeasuredValueShortWithCP56Time2a)object),
                                0);
        CS101_ASDU_addInformationObject(asdu, object);
        const bool success = peer ? IMasterConnection_sendASDU(peer, asdu) : true;
        if (!peer) CS104_Slave_enqueueASDU(server, asdu);
        InformationObject_destroy(object);
        CS101_ASDU_destroy(asdu);
        return success;
    }

    static bool gi(void* parameter, IMasterConnection peer, CS101_ASDU request, uint8_t) {
        auto* self = static_cast<Simulator*>(parameter);
        ++self->interrogations;
        IMasterConnection_sendACT_CON(peer, request, false);
        for (const auto type : kMonitoring)
            self->send(peer, CS101_ASDU_getCA(request), 100 + type, type, monitoring_value(type),
                       CS101_COT_INTERROGATED_BY_STATION, 0, CS101_ASDU_getOA(request));
        self->send(peer, CS101_ASDU_getCA(request), 900, 13, 1, CS101_COT_INTERROGATED_BY_STATION,
                   0x80);
        self->send(peer, CS101_ASDU_getCA(request), 901, 3, 0, CS101_COT_INTERROGATED_BY_STATION);
        self->send(peer, CS101_ASDU_getCA(request), 902, 36, 1, CS101_COT_INTERROGATED_BY_STATION,
                   0, 0, true);
        IMasterConnection_sendACT_TERM(peer, request);
        return true;
    }

    static bool read(void* parameter, IMasterConnection, CS101_ASDU, int) {
        ++static_cast<Simulator*>(parameter)->reads;
        return true;
    }

    static bool command(void* parameter, IMasterConnection peer, CS101_ASDU request) {
        auto* self = static_cast<Simulator*>(parameter);
        auto object = CS101_ASDU_getElement(request, 0);
        if (!object) return false;
        const int type = CS101_ASDU_getTypeID(request);
        bool select = false;
        double value = 0;
        switch (type) {
            case 45:
            case 58:
                select = SingleCommand_isSelect((SingleCommand)object);
                value = SingleCommand_getState((SingleCommand)object);
                break;
            case 46:
            case 59:
                select = DoubleCommand_isSelect((DoubleCommand)object);
                value = DoubleCommand_getState((DoubleCommand)object);
                break;
            case 47:
            case 60:
                select = StepCommand_isSelect((StepCommand)object);
                value = StepCommand_getState((StepCommand)object);
                break;
            case 48:
            case 61:
                select = SetpointCommandNormalized_isSelect((SetpointCommandNormalized)object);
                value = SetpointCommandNormalized_getValue((SetpointCommandNormalized)object);
                break;
            case 49:
            case 62:
                select = SetpointCommandScaled_isSelect((SetpointCommandScaled)object);
                value = SetpointCommandScaled_getValue((SetpointCommandScaled)object);
                break;
            case 50:
            case 63:
                select = SetpointCommandShort_isSelect((SetpointCommandShort)object);
                value = SetpointCommandShort_getValue((SetpointCommandShort)object);
                break;
        }
        InformationObject_destroy(object);
        if (select)
            ++self->selections;
        else
            ++self->executions;
        self->last_command = value;
        const int mode = self->mode;
        if (mode == 2) return true;                   // 丢弃应答，模拟执行结果未知。
        if (mode == 3) CS101_ASDU_setCA(request, 2);  // 其他站的确认不能匹配。
        if (mode == 5) CS101_ASDU_setTest(request, true);
        IMasterConnection_sendACT_CON(peer, request, mode == 1);
        if (!select && mode != 1 && mode != 4) IMasterConnection_sendACT_TERM(peer, request);
        return true;
    }
};

struct NativeClient {
    CS104_Connection connection{nullptr};
    std::atomic<bool> ready{false};
    std::atomic<int> termination{0}, negative{0};
    std::mutex mutex;
    std::map<std::tuple<int, int, int>, double> values;
    std::atomic<std::int64_t> device_timestamp{0};

    explicit NativeClient(int port) {
        connection = CS104_Connection_create("127.0.0.1", port);
        CHECK(connection);
        CS104_Connection_setConnectionHandler(connection, event, this);
        CS104_Connection_setASDUReceivedHandler(connection, receive, this);
        CHECK(CS104_Connection_connect(connection));
        CS104_Connection_sendStartDT(connection);
        eventually([&] { return ready.load(); });
    }

    ~NativeClient() { CS104_Connection_destroy(connection); }

    static void event(void* parameter, CS104_Connection, CS104_ConnectionEvent event) {
        if (event == CS104_CONNECTION_STARTDT_CON_RECEIVED)
            static_cast<NativeClient*>(parameter)->ready = true;
    }

    static bool receive(void* parameter, int, CS101_ASDU asdu) {
        auto* self = static_cast<NativeClient*>(parameter);
        const int type = CS101_ASDU_getTypeID(asdu);
        if (CS101_ASDU_isNegative(asdu)) ++self->negative;
        if (type == 100 && CS101_ASDU_getCOT(asdu) == CS101_COT_ACTIVATION_TERMINATION)
            ++self->termination;
        if (type != 1 && type != 13 && type != 36) return true;
        std::lock_guard<std::mutex> lock(self->mutex);
        for (int i = 0; i < CS101_ASDU_getNumberOfElements(asdu); ++i) {
            auto object = CS101_ASDU_getElement(asdu, i);
            if (!object) continue;
            const double value =
                type == 1 ? SinglePointInformation_getValue((SinglePointInformation)object)
                          : MeasuredValueShort_getValue((MeasuredValueShort)object);
            self->values[{CS101_ASDU_getCA(asdu), InformationObject_getObjectAddress(object),
                          type}] = value;
            if (type == 36)
                self->device_timestamp = static_cast<std::int64_t>(
                    CP56Time2a_toMsTimestamp(MeasuredValueShortWithCP56Time2a_getTimestamp(
                        (MeasuredValueShortWithCP56Time2a)object)));
            InformationObject_destroy(object);
        }
        return true;
    }

    bool has(int ca, int ioa, int type, double value) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = values.find({ca, ioa, type});
        return it != values.end() && it->second == value;
    }

    void gi(int ca) {
        CHECK(CS104_Connection_sendInterrogationCommand(connection, CS101_COT_ACTIVATION, ca, 20));
    }
};

void lazy_load() {
    PluginManager manager;
    configure_manager(manager);
    CHECK(manager.loaded_library_count() == 0);
    {
        Plugin first(manager), second(manager);
        CHECK(manager.loaded_library_count() == 1);
        CHECK(first.instance != second.instance);
        CHECK(std::string(first.functions.info_fn()->protocol_name) == "IEC104");
        CHECK(first.functions.is_connected_fn(first.instance) == 0);
        CHECK(first.functions.connect_fn(first.instance) < 0);
        CHECK(first.functions.read_batch_fn(nullptr, nullptr, 0, nullptr, 0) < 0);
    }
    CHECK(manager.loaded_library_count() == 0);
    const auto source = fs::absolute(fs::u8path(plugin_path));
    const auto unicode = source.parent_path() / fs::u8path(u8"IEC104 中文路径");
    fs::create_directories(unicode);
    for (const auto& item : fs::directory_iterator(source.parent_path()))
        if (item.is_regular_file() && (item.path().extension() == ".dll" || item.path() == source))
            fs::copy_file(item.path(), unicode / item.path().filename(),
                          fs::copy_options::overwrite_existing);
    manager.set_plugin_dir(unicode.u8string());
    {
        Plugin instance(manager);
    }
    CHECK(manager.loaded_library_count() == 0);
}

void config() {
    PluginManager manager;
    configure_manager(manager);
    Plugin instance(manager);
    const std::vector<Json> invalid = {{{"common_addresses", {0}}},
                                       {{"common_addresses", {1, 1}}},
                                       {{"port", 0}},
                                       {{"send_window_size", 1}, {"receive_window_size", 2}},
                                       {{"confirm_interval", 15}},
                                       {{"tls", true}},
                                       {{"local_role", "invalid"}},
                                       {{"timeout_ms", 0}},
                                       {{"points", {{{"code", "1:1:99"}}}}},
                                       {{"points", {{{"code", "1:1:1"}, {"value", 2}}}}},
                                       {{"points", {{{"code", "1:1:9"}, {"value", 1}}}}},
                                       {{"points", {{{"code", "1:1:13"}, {"quality", 2}}}}}};
    for (const auto& item : invalid) {
        CHECK(instance.functions.init_fn(instance.instance, item.dump().c_str()) < 0);
        CHECK(!instance.error().empty());
    }
    CHECK(instance.functions.init_fn(instance.instance, "not-json") < 0);
    CHECK(instance.functions.init_fn(instance.instance, nullptr) < 0);
    CHECK(instance.functions.init_fn(
              instance.instance, "{\"cp56_timezone_offset_minutes\":18446744073709551615}") < 0);
    CHECK(instance.functions.init_fn(
              instance.instance,
              "{\"points\":[{\"code\":\"1:1:13\",\"timestamp\":1767225600123.5}]}") < 0);
    Simulator simulator;
    Json legacy = {{"protocol", "IEC104"},
                   {"connection", "127.0.0.1:" + std::to_string(simulator.port)},
                   {"settings", "{\"common_addresses\":[1,2],\"originator_address\":7}"}};
    instance.init(legacy);
    instance.connect();
    CHECK(instance.functions.init_fn(instance.instance, "{}") < 0);
}

void monitoring_types() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    client.init(client_config(simulator.port));
    client.connect();
    std::vector<std::string> codes;
    for (int ca : {1, 2})
        for (int type : kMonitoring)
            codes.push_back(std::to_string(ca) + ":" + std::to_string(100 + type) + ":" +
                            std::to_string(type));
    eventually([&] {
        const auto rows = client.read(codes);
        return std::all_of(rows.begin(), rows.end(),
                           [](const Json& row) { return row.at("quality") == 0; });
    });
    const auto rows = client.read(codes);
    for (std::size_t i = 0; i < codes.size(); ++i) {
        CHECK(rows.at(i).at("code") == codes[i]);
        const int type = kMonitoring[i % kMonitoring.size()];
        CHECK(std::abs(rows.at(i).at("value").get<double>() - monitoring_value(type)) < 0.0001);
        if (type >= 30) {
            CHECK(rows.at(i).at("timestamp") == kTimestamp);
            CHECK(rows.at(i).at("timestamp_source") == "device_cp56");
        }
    }
    CHECK(simulator.interrogations == 2);
    CHECK(simulator.reads == 0);
}

void client_cache() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    auto config = client_config(simulator.port);
    config["stale_ms"] = 100;
    client.init(config);
    client.connect();
    eventually([&] { return client.read({"1:113:M_ME_NC_1"})[0]["quality"] == 0; });
    const auto invalid =
        client.read({"1:900:13", "1:901:3", "1:113:11", "3:113:13", "1:1:45", "wrong", "1:902:36"});
    for (const auto& row : invalid) {
        CHECK(row["quality"] == 1);
        CHECK(row["value"].is_null());
    }
    CHECK(invalid[0]["iec_quality"] == 128);
    std::this_thread::sleep_for(120ms);
    CHECK(client.read({"1:113:13"})[0]["stale"] == true);
    simulator.send(nullptr, 1, 113, 13, 99, CS101_COT_SPONTANEOUS);
    eventually([&] { return client.read({"1:113:13"})[0]["value"] == 99; });
    CHECK(simulator.reads == 0);
    CHECK(simulator.interrogations == 2);
}

void time_and_window() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    auto config = client_config(simulator.port);
    config["send_window_size"] = 1;
    config["receive_window_size"] = 1;
    config["cp56_timezone_offset_minutes"] = 480;
    client.init(config);
    client.connect();
    eventually([&] { return client.read({"2:136:36"})[0]["quality"] == 0; });
    CHECK(client.read({"2:136:36"})[0]["timestamp"] == kTimestamp - 480 * 60000LL);
    CHECK(simulator.interrogations == 2);
    client.functions.disconnect_fn(client.instance);
    config["max_points"] = 1;
    client.init(config);
    client.connect();
    eventually([&] { return simulator.interrogations == 4; });
    CHECK(client.read({"1:101:1"})[0]["quality"] == 0);
    CHECK(client.read({"1:113:13"})[0]["value"].is_null());
    CHECK(client.error().find("cache full") != std::string::npos);
}

void commands() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    auto config = client_config(simulator.port);
    config["select_execute"] = true;
    client.init(config);
    client.connect();
    for (int type : kCommands) {
        const double value = type == 45 || type == 58                               ? 1
                             : type == 46 || type == 47 || type == 59 || type == 60 ? 2
                             : type == 48 || type == 61                             ? -0.5
                             : type == 49 || type == 62                             ? -1234
                                                                                    : 12.5;
        if (client.write("2:100:" + std::to_string(type), value))
            throw std::runtime_error(client.error());
        CHECK(std::abs(simulator.last_command.load() - value) < 0.0001);
    }
    CHECK(simulator.selections == 12);
    CHECK(simulator.executions == 12);
    const int before = simulator.executions;
    CHECK(client.write("1:100:45", 2) < 0);
    CHECK(client.write("1:100:46", 0) < 0);
    CHECK(client.write("1:100:49", 1.5) < 0);
    CHECK(client.write("1:100:13", 1) < 0);
    CHECK(simulator.executions == before);
    simulator.mode = 1;
    CHECK(client.write("1:100:45", 1) < 0);
    CHECK(client.error().find("negatively") != std::string::npos);
    CHECK(simulator.executions == before);  // 选择被拒绝后不得执行。
}

void command_timeout() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    auto config = client_config(simulator.port);
    config["command_timeout_ms"] = 120;
    for (const int mode : {2, 3, 4, 5}) {
        simulator.mode = mode;
        client.init(config);
        client.connect();
        CHECK(client.write("1:100:45", 1) < 0);
        CHECK(client.error().find("unknown") != std::string::npos);
        CHECK(client.functions.is_connected_fn(client.instance) == 0);
    }
    CHECK(simulator.executions == 4);  // 每次只发送一条，绝不自动重发。
    simulator.mode = 4;
    config["command_require_termination"] = false;
    client.init(config);
    client.connect();
    CHECK(client.write("1:100:45", 1) == 0);
}

void server() {
    PluginManager manager;
    configure_manager(manager);
    Plugin server(manager);
    const int port = unused_port();
    Json config = {{"host", "127.0.0.1"},
                   {"port", port},
                   {"local_role", "server"},
                   {"common_addresses", {1, 2}},
                   {"cp56_timezone_offset_minutes", 480},
                   {"points",
                    {{{"code", "1:100:13"}, {"value", 12.5}},
                     {{"code", "2:100:13"}, {"value", 25}},
                     {{"code", "1:101:1"}, {"value", 1}},
                     {{"code", "1:102:36"}, {"value", 10}, {"timestamp", kTimestamp}}}}};
    server.init(config);
    server.connect();
    Plugin blocked(manager);
    blocked.init(config);
    CHECK(blocked.functions.connect_fn(blocked.instance) < 0);
    NativeClient first(port), second(port);
    first.gi(1);
    first.gi(2);
    second.gi(1);
    eventually([&] { return first.termination == 2 && second.termination == 1; });
    CHECK(first.has(1, 100, 13, 12.5));
    CHECK(first.has(2, 100, 13, 25));
    CHECK(first.has(1, 101, 1, 1));
    CHECK(first.device_timestamp == kTimestamp + 480 * 60000LL);
    CHECK(server.write("1:100:13", 50) == 0);
    eventually([&] { return first.has(1, 100, 13, 50) && second.has(1, 100, 13, 50); });
    CHECK(server.write("1:999:13", 1) < 0);
    auto object = SingleCommand_create(nullptr, 100, true, false, 0);
    CHECK(CS104_Connection_sendProcessCommandEx(first.connection, CS101_COT_ACTIVATION, 1,
                                                (InformationObject)object));
    SingleCommand_destroy(object);
    eventually([&] { return first.negative > 0; });
    CHECK(server.read({"1:100:13"})[0]["value"] == 50);
    CHECK(CS104_Connection_sendReadCommand(second.connection, 2, 100));
    eventually([&] { return second.has(2, 100, 13, 25); });
    first.gi(3);
    eventually([&] { return first.negative > 1; });
}

void buffer_retry() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    client.init(client_config(simulator.port));
    client.connect();
    eventually([&] { return client.read({"1:113:13"})[0]["value"] == 12.5; });
    const char* code = "1:113:13";
    char tiny[2] = {'x', 'x'};
    CHECK(client.functions.read_batch_fn(client.instance, &code, 1, tiny, sizeof(tiny)) == -2);
    CHECK(tiny[0] == '\0');
    simulator.send(nullptr, 1, 113, 13, 99, CS101_COT_SPONTANEOUS);
    std::this_thread::sleep_for(100ms);
    char output[2048];
    CHECK(client.functions.read_batch_fn(client.instance, &code, 1, output, sizeof(output)) == 1);
    CHECK(Json::parse(output)[0]["value"] == 12.5);  // 扩容必须重取同一次快照。
    eventually([&] { return client.read({code})[0]["value"] == 99; });
    CHECK(simulator.interrogations == 2);
    CHECK(simulator.reads == 0);
}

void reconnect() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    Plugin client(manager);
    client.init(client_config(simulator.port));
    client.connect();
    eventually([&] { return client.read({"1:113:13"})[0]["quality"] == 0; });
    CS104_Slave_stop(simulator.server);
    eventually([&] { return !client.functions.is_connected_fn(client.instance); });
    CHECK(client.functions.disconnect_fn(client.instance) == 0);
    auto config = client_config(simulator.port);
    config["general_interrogation_on_connect"] = false;
    client.init(config);
    CS104_Slave_start(simulator.server);
    client.connect();
    CHECK(client.read({"1:113:13"})[0]["value"].is_null());
    simulator.send(nullptr, 1, 113, 13, 77, CS101_COT_SPONTANEOUS);
    eventually([&] { return client.read({"1:113:13"})[0]["value"] == 77; });
    CHECK(client.functions.disconnect_fn(client.instance) == 0);
    CHECK(client.functions.disconnect_fn(client.instance) == 0);
}

void concurrent_instances() {
    Simulator simulator;
    PluginManager manager;
    configure_manager(manager);
    auto first = std::make_unique<Plugin>(manager);
    Plugin second(manager);
    first->init(client_config(simulator.port));
    second.init(client_config(simulator.port));
    first->connect();
    second.connect();
    CHECK(manager.loaded_library_count() == 1);
    eventually([&] {
        return first->read({"1:113:13"})[0]["quality"] == 0 &&
               second.read({"1:113:13"})[0]["quality"] == 0;
    });
    first.reset();
    CHECK(manager.loaded_library_count() == 1);
    simulator.send(nullptr, 1, 113, 13, 88, CS101_COT_SPONTANEOUS);
    eventually([&] { return second.read({"1:113:13"})[0]["value"] == 88; });
}
}  // namespace

int main(int argc, char** argv) {
    static_assert(sizeof(ems::net::protocol::Iec104Protocol) == sizeof(void*));
    try {
        CHECK(argc >= 2);
        if (argc == 3) plugin_path = argv[2];
        const std::map<std::string, void (*)()> scenarios = {
            {"lazy_load", lazy_load},
            {"config", config},
            {"monitoring_types", monitoring_types},
            {"client_cache", client_cache},
            {"time_and_window", time_and_window},
            {"commands", commands},
            {"command_timeout", command_timeout},
            {"server", server},
            {"buffer_retry", buffer_retry},
            {"reconnect", reconnect},
            {"concurrent_instances", concurrent_instances}};
        scenarios.at(argv[1])();
        std::cout << "PASS " << argv[1] << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
