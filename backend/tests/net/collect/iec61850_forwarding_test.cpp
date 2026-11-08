#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <iec61850_client.h>
#include <iec61850_dynamic_model.h>
#include <iec61850_server.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#include "common/log/process_logger.hpp"
#include "net/collect/collect_engine.h"
#include "rtdb/backend/shared_memory_store.h"
#include "rtdb/client/realtime_client.h"
#include "rtdb/client/rtdb_manager.h"

using Json = nlohmann::json;
using namespace ems;
namespace fs = std::filesystem;
#define CHECK(condition)                                                                          \
    do {                                                                                          \
        if (!(condition)) throw std::runtime_error(std::to_string(__LINE__) + ": " + #condition); \
    } while (false)

namespace {
std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int unused_port() {
#ifdef _WIN32
    WSADATA startup{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &startup) == 0);
#endif
    const auto socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    CHECK(getsockname(socket_fd, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const auto port = ntohs(address.sin_port);
#ifdef _WIN32
    closesocket(socket_fd);
    WSACleanup();
#else
    close(socket_fd);
#endif
    return port;
}

template <class Predicate>
void eventually(Predicate condition) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(condition());
}

struct Storage {
    const std::string name =
        "ems_forwarding_test_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path path = fs::temp_directory_path() / (name + ".json");
    std::unique_ptr<rtdb::RealtimeClient> owner;

    Storage() {
        const Json settings = {{"realtime",
                                {{"backend",
                                  {{"type", "shm"},
                                   {"options",
                                    {{"segment_name", name},
                                     {"capacity", 32},
                                     {"shard_count", 2},
                                     {"segment_bytes", 8 * 1024 * 1024}}}}}}}};
        std::ofstream(path) << settings.dump();
        auto opened = rtdb::RealtimeClient::initialize(
            utf8(path), core::make_call_context(std::chrono::seconds(2)));
        CHECK(core::is_ok(opened));
        owner = std::get<0>(std::move(opened));
    }

    ~Storage() {
        owner.reset();
        rtdb::SharedMemoryStore::remove(name);
        std::error_code error;
        fs::remove(path, error);
    }
};

struct Source {
    IedModel* model = IedModel_create("SOURCE");
    IedServer server{nullptr};
    DataAttribute *value, *quality, *time, *status;
    const int port = unused_port();
    std::uint64_t timestamp{now_ms()};

    Source() {
        auto* device = LogicalDevice_create("LD0", model);
        auto* node = LogicalNode_create("MMXU1", device);
        auto* object = DataObject_create("TotW", reinterpret_cast<ModelNode*>(node), 0);
        auto* magnitude = DataAttribute_create("mag", reinterpret_cast<ModelNode*>(object),
                                               IEC61850_CONSTRUCTED, IEC61850_FC_MX, 0, 0, 0);
        value = DataAttribute_create("f", reinterpret_cast<ModelNode*>(magnitude), IEC61850_FLOAT32,
                                     IEC61850_FC_MX, TRG_OPT_DATA_CHANGED, 0, 0);
        quality = DataAttribute_create("q", reinterpret_cast<ModelNode*>(object), IEC61850_QUALITY,
                                       IEC61850_FC_MX, TRG_OPT_QUALITY_CHANGED, 0, 0);
        time = DataAttribute_create("t", reinterpret_cast<ModelNode*>(object), IEC61850_TIMESTAMP,
                                    IEC61850_FC_MX, 0, 0, 0);
        auto* state = DataObject_create("State", reinterpret_cast<ModelNode*>(node), 0);
        status = DataAttribute_create("stVal", reinterpret_cast<ModelNode*>(state),
                                      IEC61850_BOOLEAN, IEC61850_FC_ST, TRG_OPT_DATA_CHANGED, 0, 0);
        server = IedServer_create(model);
        update(21);
        IedServer_setLocalIpAddress(server, "127.0.0.1");
        IedServer_start(server, port);
        CHECK(IedServer_isRunning(server));
    }

    void update(float number) {
        timestamp = now_ms();
        IedServer_lockDataModel(server);
        IedServer_updateFloatAttributeValue(server, value, number);
        IedServer_updateQuality(server, quality, 0);
        IedServer_updateUTCTimeAttributeValue(server, time, timestamp);
        IedServer_updateBooleanAttributeValue(server, status, false);
        IedServer_unlockDataModel(server);
    }

    ~Source() {
        IedServer_stop(server);
        IedServer_destroy(server);
        IedModel_destroy(model);
    }
};

database::DeviceConfigRecords configuration(int source_port, int output_port) {
    database::DeviceConfigRecords rows;
    database::CfgDeviceRow station;
    station.id = 1;
    station.device_id = "station";
    station.node_kind = "station";
    station.config_revision = 1;
    rows.devices.push_back(station);
    auto node = station;
    node.id = 2;
    node.device_id = "meter";
    node.parent_device_id = "station";
    node.node_kind = "device";
    rows.devices.push_back(node);
    database::CfgChannelRow source;
    source.id = 7;
    source.channel_id = "source";
    source.name = "ForwardingSource";
    source.protocol = "iec61850_mms";
    source.conn_mode = "tcp_client";
    source.endpoint = "127.0.0.1:" + std::to_string(source_port);
    source.poll_interval_ms = 25;
    source.timeout_ms = 500;
    rows.channels.push_back(source);
    auto output = source;
    output.id = 8;
    output.channel_id = "output";
    output.conn_mode = "tcp_server";
    output.endpoint = "127.0.0.1:" + std::to_string(output_port);
    output.serial_json =
        Json{{"iec61850",
              {{"points", Json::array({{{"code", "power"},
                                        {"object_ref", "EMSLD0/MMXU1.TotW.mag.f"},
                                        {"fc", "MX"},
                                        {"mms_type", "float32"},
                                        {"quality_ref", "EMSLD0/MMXU1.TotW.q"},
                                        {"timestamp_ref", "EMSLD0/MMXU1.TotW.t"}}})},
               {"datasets",
                Json::array({{{"dataset_ref", "EMSLD0/LLN0$measurements"},
                              {"members", Json::array({{{"object_ref", "EMSLD0/MMXU1.TotW"},
                                                        {"fc", "MX"}}})}}})},
               {"reports", Json::array({{{"rcb_ref", "EMSLD0/LLN0.RP.urcb01"},
                                         {"dataset_ref", "EMSLD0/LLN0$measurements"}}})}}},
             {"forwarding", Json::array({{{"code", "power"},
                                          {"source_point_id", "point-power"},
                                          {"stale_after_ms", 2000}}})}}
            .dump();
    rows.channels.push_back(output);
    database::CfgBindingRow binding;
    binding.id = 1;
    binding.binding_id = "input-binding";
    binding.device_id = "meter";
    binding.channel_id = "source";
    binding.can_collect = 1;
    rows.bindings.push_back(binding);
    binding.id = 2;
    binding.binding_id = "output-binding";
    binding.channel_id = "output";
    binding.can_collect = 0;
    binding.can_forward = 1;
    rows.bindings.push_back(binding);
    database::CfgPointYcRow point;
    point.id = 1;
    point.point_id = "point-power";
    point.code = "power";
    point.owner_device_id = "meter";
    point.binding_id = "input-binding";
    point.wire_type = "f32";
    point.scale_factor = 2;
    point.offset_value = 1;
    point.protocol_address_json =
        R"({"object_ref":"SOURCELD0/MMXU1.TotW.mag.f","fc":"MX","quality_ref":"SOURCELD0/MMXU1.TotW.q","timestamp_ref":"SOURCELD0/MMXU1.TotW.t"})";
    rows.points_yc.push_back(point);
    database::CfgPointYxRow state;
    state.id = 2;
    state.point_id = "point-status";
    state.code = "state";
    state.owner_device_id = "meter";
    state.binding_id = "input-binding";
    state.wire_type = "bool";
    state.invert = 1;
    state.protocol_address_json = R"({"object_ref":"SOURCELD0/MMXU1.State.stVal","fc":"ST"})";
    rows.points_yx.push_back(state);
    auto settings = Json::parse(rows.channels[1].serial_json);
    settings["iec61850"]["points"].push_back({{"code", "status"},
                                              {"object_ref", "EMSLD0/MMXU1.State.stVal"},
                                              {"fc", "ST"},
                                              {"mms_type", "boolean"},
                                              {"quality_ref", "EMSLD0/MMXU1.State.q"},
                                              {"timestamp_ref", "EMSLD0/MMXU1.State.t"}});
    settings["forwarding"].push_back(
        {{"code", "status"}, {"source_point_id", "point-status"}, {"stale_after_ms", 2000}});
    rows.channels[1].serial_json = settings.dump();
    return rows;
}

void run() {
    Storage storage;
    Source source;
    const auto port = unused_port();
    auto rows = configuration(source.port, port);
    net::device::CfgLoader loader;
    CHECK(loader.configure(rows));
    net::collect::EngineConfig options;
    options.rtdbConfigPath_ = utf8(storage.path);
    options.rtdbScopeCode_ = "teststation";
    options.pluginDir_ = utf8(fs::path(EMS_FORWARDING_PLUGIN).parent_path());
    options.notificationBackend_ = net::collect::NotificationBackend::None;
    net::collect::CollectEngine engine;
    CHECK(engine.initialize(options));
    CHECK(engine.start_all(loader) == 2);
    CHECK(engine.channel_count() == 2);
    auto writer = engine.data_dispatcher()->rtdb_manager()->getRtDb();
    auto connection =
        std::shared_ptr<sIedConnection>(IedConnection_create(), [](IedConnection value) {
            IedConnection_close(value);
            IedConnection_destroy(value);
        });
    IedClientError error;
    IedConnection_setRequestTimeout(connection.get(), 500);
    IedConnection_connect(connection.get(), &error, "127.0.0.1", port);
    CHECK(error == IED_ERROR_OK);
    auto value = [&] {
        return IedConnection_readFloatValue(connection.get(), &error, "EMSLD0/MMXU1.TotW.mag.f",
                                            IEC61850_FC_MX);
    };
    auto quality = [&] {
        return IedConnection_readQualityValue(connection.get(), &error, "EMSLD0/MMXU1.TotW.q",
                                              IEC61850_FC_MX);
    };
    eventually([&] { return value() == 43.0f && quality() == 0; });
    auto status = [&] {
        return IedConnection_readBooleanValue(connection.get(), &error, "EMSLD0/MMXU1.State.stVal",
                                              IEC61850_FC_ST);
    };
    auto status_quality = [&] {
        return IedConnection_readQualityValue(connection.get(), &error, "EMSLD0/MMXU1.State.q",
                                              IEC61850_FC_ST);
    };
    eventually([&] { return status() && status_quality() == 0; });
    Timestamp time{};
    CHECK(IedConnection_readTimestampValue(connection.get(), &error, "EMSLD0/MMXU1.TotW.t",
                                           IEC61850_FC_MX, &time));
    CHECK(Timestamp_getTimeInMs(&time) == source.timestamp);
    // 使用另一个插件实例订阅上送 URCB，覆盖自动更新触发报告的链路。
    auto& manager = engine.plugin_manager();
    auto* subscriber = manager.load("IEC61850");
    CHECK(subscriber);
    const auto api = manager.get_instance_functions(subscriber);
    const auto client_options = Json::parse(
        R"({"read_mode":"report","points":[{"code":"power","object_ref":"EMSLD0/MMXU1.TotW.mag.f","fc":"MX","quality_ref":"EMSLD0/MMXU1.TotW.q","timestamp_ref":"EMSLD0/MMXU1.TotW.t"}],"reports":[{"rcb_ref":"EMSLD0/LLN0.RP.urcb01","dataset_ref":"EMSLD0/LLN0$measurements"}]})");
    const Json client{{"connection", "127.0.0.1:" + std::to_string(port)},
                      {"settings", {{"iec61850", client_options}}}};
    CHECK(api.init_fn(subscriber, client.dump().c_str()) == 0);
    CHECK(api.connect_fn(subscriber) == 0);
    auto reported = [&] {
        char output[8192]{};
        const char* codes[] = {"power"};
        CHECK(api.read_batch_fn(subscriber, codes, 1, output, sizeof(output)) >= 0);
        return Json::parse(output)[0];
    };
    source.update(35);
    eventually([&] { return reported()["value"] == 71.0; });
    CHECK(reported()["timestamp"] == source.timestamp);
    CHECK(manager.release(subscriber));
    CHECK(engine.stop_channel(7));
    model::YxPoint binary;
    binary.code_ = "point-status";
    binary.timestamp_ = now_ms();
    binary.value_ = 2;
    CHECK(writer->setYx(binary.code_, binary));
    eventually([&] { return (status_quality() & 3) != 0; });
    CHECK(status());
    binary.value_ = 0;
    CHECK(writer->setYx(binary.code_, binary));
    eventually([&] { return !status() && status_quality() == 0; });
    model::YcPoint sample;
    sample.code_ = "point-power";
    sample.value_ = 99;
    sample.timestamp_ = now_ms() - 10000;
    CHECK(writer->setYc(sample.code_, sample));
    eventually([&] { return (quality() & 129) == 129; });
    sample.timestamp_ = now_ms();
    sample.value_ = 101;
    CHECK(writer->setYc(sample.code_, sample));
    eventually([&] { return value() == 101 && quality() == 0; });
    CHECK(writer->setHashField("rtdb:yc:point-power", "json", "broken"));
    eventually([&] { return (quality() & 3) != 0; });
    CHECK(value() == 101);
    CHECK(writer->deleteYc("point-power"));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK((quality() & 3) != 0);
    sample.timestamp_ = now_ms();
    sample.value_ = 123;
    sample.quality_ = model::PointQuality::Invalid;
    CHECK(writer->setYc(sample.code_, sample));
    eventually([&] { return value() == 123 && (quality() & 3) != 0; });
    sample.quality_ = model::PointQuality::Good;
    CHECK(writer->setYc(sample.code_, sample));
    eventually([&] { return quality() == 0; });
    auto* denied = MmsValue_newFloat(999);
    IedConnection_writeObject(connection.get(), &error, "EMSLD0/MMXU1.TotW.mag.f", IEC61850_FC_MX,
                              denied);
    MmsValue_delete(denied);
    CHECK(error != IED_ERROR_OK);
    model::YcPoint stored;
    CHECK(writer->getYc("point-power", stored));
    CHECK(stored.value_ == 123);
    connection.reset();
    engine.stop_all();
    CHECK(manager.loaded_library_count() == 0);
    rows.bindings[0].enabled = 0;
    ++rows.devices[0].config_revision;
    CHECK(loader.configure(rows));
    CHECK(engine.start_all(loader) == 1);
    auto fresh = std::shared_ptr<sIedConnection>(IedConnection_create(), [](IedConnection peer) {
        IedConnection_close(peer);
        IedConnection_destroy(peer);
    });
    IedConnection_connect(fresh.get(), &error, "127.0.0.1", port);
    CHECK(error == IED_ERROR_OK);
    CHECK((IedConnection_readQualityValue(fresh.get(), &error, "EMSLD0/MMXU1.TotW.q",
                                          IEC61850_FC_MX) &
           3) != 0);
    fresh.reset();
    engine.shutdown();
    CHECK(manager.loaded_library_count() == 0);
}
}  // namespace

int main() {
    const ProcessLogger logger("iec61850-forwarding-test");
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
