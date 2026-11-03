#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <iec61850_client.h>
#include <iec61850_dynamic_model.h>
#include <iec61850_server.h>
#include <mms_server.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#include "net/protocol/plugin_manager.h"
#ifdef EMS_IEC61850_HANDLER_TEST
#include "net/handler/protocol_handler.h"
#endif

using Json = nlohmann::json;
using ems::net::collect::PluginManager;
namespace fs = std::filesystem;

// 故障帧夹具直接调用固定入库 C 栈的内部发送原语；生产插件不依赖这些接口。
extern "C" void* private_ClientConnection_getServerConnectionHandle(ClientConnection connection);
extern "C" void MmsServerConnection_sendInformationReportVMDSpecific(MmsServerConnection connection,
                                                                     char* name, LinkedList values,
                                                                     bool handler_mode);

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition))                                                                     \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                                     ": " + #condition);                                      \
    } while (0)

namespace {
constexpr std::uint64_t kTimestamp = 1793577600123ULL;

int unused_port() {
#ifdef _WIN32
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const auto handle = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(handle != INVALID_SOCKET);
    int length = sizeof(sockaddr_in);
#else
    const auto handle = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(handle >= 0);
    socklen_t length = sizeof(sockaddr_in);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    CHECK(getsockname(handle, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const int port = ntohs(address.sin_port);
#ifdef _WIN32
    closesocket(handle);
    WSACleanup();
#else
    close(handle);
#endif
    return port;
}

struct Server {
    IedModel* model{IedModel_create("SIM")};
    IedServer server{nullptr};
    DataAttribute *power, *quality, *timestamp, *status, *large, *array, *unsigned_value,
        *double_value;
    const int port{unused_port()};
    std::atomic<MmsServerConnection> peer{nullptr};

    Server() {
        auto* ld = LogicalDevice_create("LD0", model);
        auto* lln = LogicalNode_create("LLN0", ld);
        auto* ln = LogicalNode_create("MMXU1", ld);
        auto* watts = DataObject_create("TotW", reinterpret_cast<ModelNode*>(ln), 0);
        auto* mag = DataAttribute_create(
            "mag", reinterpret_cast<ModelNode*>(watts), IEC61850_CONSTRUCTED, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        power = DataAttribute_create(
            "f", reinterpret_cast<ModelNode*>(mag), IEC61850_FLOAT32, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        quality = DataAttribute_create(
            "q", reinterpret_cast<ModelNode*>(watts), IEC61850_QUALITY, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        timestamp = DataAttribute_create(
            "t", reinterpret_cast<ModelNode*>(watts), IEC61850_TIMESTAMP, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        auto* binary = DataObject_create("Status", reinterpret_cast<ModelNode*>(ln), 0);
        status =
            DataAttribute_create("stVal", reinterpret_cast<ModelNode*>(binary), IEC61850_BOOLEAN,
                                 IEC61850_FC_ST, TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE, 0, 0);
        auto* counter = DataObject_create("Counter", reinterpret_cast<ModelNode*>(ln), 0);
        large = DataAttribute_create(
            "value", reinterpret_cast<ModelNode*>(counter), IEC61850_INT64, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        unsigned_value = DataAttribute_create(
            "unsignedValue", reinterpret_cast<ModelNode*>(counter), IEC61850_INT32U, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        double_value = DataAttribute_create(
            "doubleValue", reinterpret_cast<ModelNode*>(counter), IEC61850_FLOAT64, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0, 0);
        auto* samples = DataObject_create("Samples", reinterpret_cast<ModelNode*>(ln), 0);
        array = DataAttribute_create(
            "values", reinterpret_cast<ModelNode*>(samples), IEC61850_INT32, IEC61850_FC_MX,
            TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 3, 0);
        auto* dataset = DataSet_create("measurements", lln);
        CHECK(DataSetEntry_create(dataset, "MMXU1$ST$Status$stVal", -1, nullptr));
        CHECK(DataSetEntry_create(dataset, "MMXU1$MX$TotW", -1, nullptr));
        CHECK(DataSetEntry_create(dataset, "MMXU1$MX$Samples$values", -1, nullptr));
        const auto report_options = RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP |
                                    RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_SET |
                                    RPT_OPT_CONF_REV | RPT_OPT_BUFFER_OVERFLOW | RPT_OPT_ENTRY_ID;
        CHECK(ReportControlBlock_create("urcb01", lln, "fixture-report", false, "measurements", 1,
                                        TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED |
                                            TRG_OPT_DATA_UPDATE | TRG_OPT_INTEGRITY | TRG_OPT_GI,
                                        report_options, 0, 1000));
        CHECK(ReportControlBlock_create("brcb01", lln, "fixture-report", true, "measurements", 1,
                                        TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED |
                                            TRG_OPT_DATA_UPDATE | TRG_OPT_INTEGRITY | TRG_OPT_GI,
                                        report_options, 0, 1000));
        server = IedServer_create(model);
        CHECK(server);
        IedServer_setConnectionIndicationHandler(
            server,
            [](IedServer, ClientConnection connection, bool connected, void* state) {
                static_cast<Server*>(state)->peer.store(
                    connected ? static_cast<MmsServerConnection>(
                                    private_ClientConnection_getServerConnectionHandle(connection))
                              : nullptr);
            },
            this);
        IedServer_lockDataModel(server);
        IedServer_updateFloatAttributeValue(server, power, 125.5f);
        IedServer_updateQuality(server, quality, QUALITY_VALIDITY_GOOD);
        IedServer_updateUTCTimeAttributeValue(server, timestamp, kTimestamp);
        IedServer_updateBooleanAttributeValue(server, status, true);
        IedServer_updateInt64AttributeValue(server, large, 42);
        IedServer_updateUnsignedAttributeValue(server, unsigned_value, UINT32_MAX);
        MmsValue_setDouble(IedServer_getAttributeValue(server, double_value), -3.25);
        auto* values = IedServer_getAttributeValue(server, array);
        for (int i = 0; i < 3; ++i) MmsValue_setInt32(MmsValue_getElement(values, i), 10 + i);
        IedServer_unlockDataModel(server);
        start();
    }

    void start() {
        IedServer_start(server, port);
        CHECK(IedServer_isRunning(server));
    }

    void send_report(int sequence, bool state, int sub = -1, bool more = false,
                     bool overflow = false, int revision = 1, bool included = true,
                     bool handler_mode = false) {
        auto* connection = peer.load();
        CHECK(connection);
        auto list = LinkedList_create();
        CHECK(list);
        auto add = [&](MmsValue* value) {
            CHECK(value);
            LinkedList_add(list, value);
        };
        add(MmsValue_newVisibleString("fixture-report"));
        auto* options = MmsValue_newBitString(10);
        for (const auto bit : {1, 3, 4, 8}) MmsValue_setBitStringBit(options, bit, true);
        if (overflow) MmsValue_setBitStringBit(options, 6, true);
        if (sub >= 0) MmsValue_setBitStringBit(options, 9, true);
        add(options);
        add(MmsValue_newUnsignedFromUint32(static_cast<std::uint32_t>(sequence)));
        add(MmsValue_newVisibleString("SIMLD0/LLN0$measurements"));
        if (overflow) add(MmsValue_newBoolean(true));
        add(MmsValue_newUnsignedFromUint32(static_cast<std::uint32_t>(revision)));
        if (sub >= 0) {
            add(MmsValue_newUnsignedFromUint32(static_cast<std::uint32_t>(sub)));
            add(MmsValue_newBoolean(more));
        }
        auto* inclusion = MmsValue_newBitString(3);
        MmsValue_setBitStringBit(inclusion, 0, included);
        add(inclusion);
        if (included) {
            add(MmsValue_newBoolean(state));
            auto* reason = MmsValue_newBitString(6);
            MmsValue_setBitStringBit(reason, 1, true);
            add(reason);
        }
        char name[] = "RPT";
        MmsServerConnection_sendInformationReportVMDSpecific(connection, name, list, handler_mode);
        LinkedList_destroyDeep(list,
                               reinterpret_cast<LinkedListValueDeleteFunction>(MmsValue_delete));
    }

    ~Server() {
        IedServer_stop(server);
        IedServer_destroy(server);
        IedModel_destroy(model);
    }
};

Json config(int port, bool dataset = false) {
    Json options{
        {"read_mode", "poll"},
        {"points",
         Json::array(
             {{{"code", "power"},
               {"object_ref", "SIMLD0/MMXU1.TotW.mag.f"},
               {"fc", "MX"},
               {"mms_type", "float32"},
               {"quality_ref", "SIMLD0/MMXU1.TotW.q"},
               {"timestamp_ref", "SIMLD0/MMXU1.TotW.t"}},
              {{"code", "status"}, {"object_ref", "SIMLD0/MMXU1.Status.stVal"}, {"fc", "ST"}},
              {{"code", "large"}, {"object_ref", "SIMLD0/MMXU1.Counter.value"}, {"fc", "MX"}},
              {{"code", "array"},
               {"object_ref", "SIMLD0/MMXU1.Samples.values(1)"},
               {"fc", "MX"}}})}};
    if (dataset) options["datasets"] = {"SIMLD0/LLN0.measurements"};
    options["points"].push_back({{"code", "unsigned"},
                                 {"object_ref", "SIMLD0/MMXU1.Counter.unsignedValue"},
                                 {"fc", "MX"},
                                 {"mms_type", "unsigned"}});
    options["points"].push_back({{"code", "double"},
                                 {"object_ref", "SIMLD0/MMXU1.Counter.doubleValue"},
                                 {"fc", "MX"},
                                 {"mms_type", "float64"}});
    return {{"protocol", "IEC61850"},
            {"connection", "127.0.0.1:" + std::to_string(port)},
            {"timeout_ms", 3000},
            {"iec61850", options}};
}

void manager_config(PluginManager& manager) {
    const auto* installed = std::getenv("EMS_IEC61850_TEST_PLUGIN");
    const auto path =
        fs::absolute(fs::u8path(installed && *installed ? installed : EMS_IEC61850_PLUGIN));
    manager.set_plugin_dir(path.parent_path().u8string());
    manager.register_mapping("IEC61850", path.filename().u8string());
}

struct Plugin {
    PluginManager manager;
    void* instance;
    PluginManager::PluginFunctions api;

    Plugin() {
        manager_config(manager);
        instance = manager.load("IEC61850");
        if (!instance) throw std::runtime_error(manager.last_error());
        api = manager.get_instance_functions(instance);
    }

    ~Plugin() { manager.release(instance); }

    std::string error() { return api.last_error_fn(instance); }

    void init(const Json& value) {
        if (api.init_fn(instance, value.dump().c_str()) != 0) throw std::runtime_error(error());
    }

    void connect() {
        if (api.connect_fn(instance) != 0) throw std::runtime_error(error());
    }

    Json read(const std::vector<std::string>& codes) {
        std::vector<const char*> pointers;
        for (const auto& code : codes) pointers.push_back(code.c_str());
        std::vector<char> output(65536);
        const auto count =
            api.read_batch_fn(instance, pointers.data(), static_cast<int>(codes.size()),
                              output.data(), static_cast<int>(output.size()));
        if (count < 0) throw std::runtime_error(error());
        auto result = Json::parse(output.data());
        int valid = 0;
        for (const auto& item : result)
            if (item["quality"] == 0) ++valid;
        CHECK(count == valid);
        return result;
    }
};

template <class Predicate>
void eventually(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(predicate());
}

Json report_config(int port, bool buffered = false, std::string mode = "report") {
    auto value = config(port, true);
    auto& options = value["iec61850"];
    auto points = Json::array();
    for (const auto& point : options["points"])
        if (point["code"] == "power" || point["code"] == "status" || point["code"] == "array")
            points.push_back(point);
    options["points"] = points;
    options["read_mode"] = mode;
    options["reports"] =
        Json::array({{{"rcb_ref", buffered ? "SIMLD0/LLN0.BR.brcb01" : "SIMLD0/LLN0.RP.urcb01"},
                      {"dataset_ref", "SIMLD0/LLN0$measurements"},
                      {"integrity_period_ms", 1000}}});
    return value;
}

Json server_config(int port) {
    Json points = Json::array({{{"code", "power"},
                                {"object_ref", "SIMLD0/MMXU1.TotW.mag.f"},
                                {"fc", "MX"},
                                {"mms_type", "float32"},
                                {"quality_ref", "SIMLD0/MMXU1.TotW.q"},
                                {"timestamp_ref", "SIMLD0/MMXU1.TotW.t"},
                                {"quality_code", "power_quality"},
                                {"value", 10.5}},
                               {{"code", "status"},
                                {"object_ref", "SIMLD0/MMXU1.Status.stVal"},
                                {"fc", "ST"},
                                {"mms_type", "boolean"},
                                {"value", true}},
                               {{"code", "setpoint"},
                                {"object_ref", "SIMLD0/GGIO1.Setpoint.setMag.f"},
                                {"fc", "SP"},
                                {"mms_type", "float32"},
                                {"value", 7.0}}});
    Json sets = Json::array(
        {{{"dataset_ref", "SIMLD0/LLN0$measurements"},
          {"members", Json::array({{{"object_ref", "SIMLD0/MMXU1.Status.stVal"}, {"fc", "ST"}},
                                   {{"object_ref", "SIMLD0/MMXU1.TotW"}, {"fc", "MX"}}})}}});
    Json reports = Json::array();
    for (const auto& name : {"RP.urcb01", "RP.urcb02", "BR.brcb01", "BR.brcb02"})
        reports.push_back({{"rcb_ref", std::string("SIMLD0/LLN0.") + name},
                           {"dataset_ref", "SIMLD0/LLN0$measurements"},
                           {"integrity_period_ms", 1000}});
    return {{"protocol", "IEC61850"},
            {"local_role", "server"},
            {"connection", "127.0.0.1:" + std::to_string(port)},
            {"iec61850",
             {{"ied_name", "EMS"}, {"points", points}, {"datasets", sets}, {"reports", reports}}}};
}

Json server_client_config(int port, bool buffered = false, int index = 1) {
    auto value = report_config(port, buffered);
    auto& points = value["iec61850"]["points"];
    points.erase(points.begin() + 2);
    value["iec61850"]["reports"][0]["rcb_ref"] =
        std::string("SIMLD0/LLN0.") + (buffered ? "BR.brcb0" : "RP.urcb0") + std::to_string(index);
    return value;
}

void run_server(const std::string& scenario) {
    const int port = unused_port();
    Plugin server;
    auto settings = server_config(port);
    if (scenario == "server_connections") settings["iec61850"]["max_connections"] = 6;
    server.init(settings);
    server.connect();
    if (scenario == "server_readonly") {
        auto* connection = IedConnection_create();
        IedClientError error;
        IedConnection_connect(connection, &error, "127.0.0.1", port);
        CHECK(error == IED_ERROR_OK);
        CHECK(IedConnection_readFloatValue(connection, &error, "SIMLD0/MMXU1.TotW.mag.f",
                                           IEC61850_FC_MX) == 10.5f);
        CHECK(error == IED_ERROR_OK);
        CHECK(server.api.write_fn(server.instance, "power", 55.0) == 0);
        CHECK(IedConnection_readFloatValue(connection, &error, "SIMLD0/MMXU1.TotW.mag.f",
                                           IEC61850_FC_MX) == 55.0f);
        auto* value = MmsValue_newFloat(999.0f);
        IedConnection_writeObject(connection, &error, "SIMLD0/GGIO1.Setpoint.setMag.f",
                                  IEC61850_FC_SP, value);
        MmsValue_delete(value);
        CHECK(error != IED_ERROR_OK);
        const auto members = LinkedList_create();
        char member[] = "SIMLD0/MMXU1.TotW.mag.f[MX]";
        LinkedList_add(members, member);
        IedConnection_createDataSet(connection, &error, "SIMLD0/LLN0$remoteCreated", members);
        LinkedList_destroyStatic(members);
        CHECK(error != IED_ERROR_OK);
        CHECK(server.read({"setpoint"})[0]["value"] == 7.0);
        CHECK(server.api.write_fn(server.instance, "status", 2) == -1);
        CHECK(server.api.write_fn(server.instance, "unknown", 5) == -1);
        CHECK(server.api.write_fn(server.instance, "power_quality", QUALITY_TEST) == 0);
        CHECK(server.read({"power"})[0]["quality"] == 1);
        CHECK(server.read({"power_quality"})[0]["value"] == QUALITY_TEST);
        CHECK(IedConnection_readQualityValue(connection, &error, "SIMLD0/MMXU1.TotW.q",
                                             IEC61850_FC_MX) == QUALITY_TEST);
        IedConnection_close(connection);
        IedConnection_destroy(connection);
    } else if (scenario == "server_connections") {
        std::vector<std::shared_ptr<sIedConnection>> peers;
        for (int i = 0; i < 7; ++i) {
            auto peer = std::shared_ptr<sIedConnection>(IedConnection_create(),
                                                        [](IedConnection connection) {
                                                            IedConnection_close(connection);
                                                            IedConnection_destroy(connection);
                                                        });
            CHECK(peer);
            IedConnection_setConnectTimeout(peer.get(), 500);
            IedConnection_setRequestTimeout(peer.get(), 500);
            IedClientError status;
            IedConnection_connect(peer.get(), &status, "127.0.0.1", port);
            CHECK(i == 6 ? status != IED_ERROR_OK : status == IED_ERROR_OK);
            peers.push_back(std::move(peer));
        }
    } else if (scenario == "server_reports") {
        Plugin first, second;
        first.init(server_client_config(port));
        first.connect();
        second.init(server_client_config(port, true));
        second.connect();
        CHECK(server.api.write_fn(server.instance, "power", 88.25) == 0);
        eventually([&] {
            return first.read({"power"})[0]["value"] == 88.25 &&
                   second.read({"power"})[0]["value"] == 88.25;
        });
        CHECK(first.api.disconnect_fn(first.instance) == 0);
        CHECK(server.api.write_fn(server.instance, "power", 123.0) == 0);
        eventually([&] { return second.read({"power"})[0]["value"] == 123.0; });
        first.connect();
        CHECK(first.read({"power"})[0]["value"] == 123.0);
    } else {
        Plugin conflict;
        conflict.init(settings);
        CHECK(conflict.api.connect_fn(conflict.instance) == -1);
        CHECK(server.api.write_fn(server.instance, "power", 42.0) == 0);
        CHECK(server.api.disconnect_fn(server.instance) == 0);
        CHECK(server.api.disconnect_fn(server.instance) == 0);
        server.connect();
        CHECK(server.read({"power"})[0]["value"] == 42.0);
    }
}

void run(const std::string& scenario) {
    if (scenario.rfind("server_", 0) == 0) {
        run_server(scenario);
        return;
    }
    if (scenario == "lazy_load") {
        PluginManager manager;
        manager_config(manager);
        CHECK(manager.loaded_library_count() == 0);
        auto* first = manager.load("IEC61850");
        auto* second = manager.load("IEC61850");
        CHECK(first && second && first != second);
        CHECK(manager.loaded_library_count() == 1);
        CHECK(manager.release(first));
        CHECK(manager.loaded_library_count() == 1);
        CHECK(manager.release(second));
        CHECK(manager.loaded_library_count() == 0);
        return;
    }
    if (scenario == "invalid_config") {
        Plugin p;
        const auto base = config(10102);
        for (int i = 0; i < 10; ++i) {
            auto value = base;
            if (i == 0) value["iec61850"]["read_mode"] = "report";
            if (i == 1) value["iec61850"]["allow_writes"] = true;
            if (i == 2) value["role"] = "server";
            if (i == 3) value["iec61850"]["points"][0]["fc"] = "CO";
            if (i == 4) value["iec61850"]["points"].push_back(value["iec61850"]["points"][0]);
            if (i == 5) value["iec61850"]["tls"] = false;
            if (i == 6) value["iec61850"]["reports"] = Json::array({Json::object()});
            if (i == 7) value["iec61850"]["points"][0]["object_ref"] = "SIMLD0/MMXU1..bad";
            if (i == 8) value["timeout_ms"] = 0;
            if (i == 9) value["connection"] = "127.0.0.1:99999";
            CHECK(p.api.init_fn(p.instance, value.dump().c_str()) == -1);
            CHECK(!p.error().empty());
        }
        p.init(base);
        CHECK(p.api.write_fn(p.instance, "power", 1) == -1);
        const std::string oversized(257, 'x');
        const char* codes[] = {oversized.c_str()};
        char output[32];
        CHECK(p.api.read_batch_fn(p.instance, codes, 1, output, sizeof(output)) == -1);
        return;
    }
    Server server;
    if (scenario.rfind("report_", 0) == 0) {
        Plugin subscriber;
        auto settings = report_config(server.port, scenario == "report_brcb");
        if (scenario == "report_isolation") {
            Server other_server;
            Plugin other;
            subscriber.init(settings);
            subscriber.connect();
            other.init(report_config(other_server.port));
            other.connect();
            IedServer_lockDataModel(server.server);
            IedServer_updateFloatAttributeValue(server.server, server.power, 111.0f);
            IedServer_unlockDataModel(server.server);
            IedServer_lockDataModel(other_server.server);
            IedServer_updateFloatAttributeValue(other_server.server, other_server.power, 333.0f);
            IedServer_unlockDataModel(other_server.server);
            eventually([&] {
                return subscriber.read({"power"})[0]["value"] == 111.0 &&
                       other.read({"power"})[0]["value"] == 333.0;
            });
            CHECK(subscriber.api.disconnect_fn(subscriber.instance) == 0);
            CHECK(other.read({"power"})[0]["value"] == 333.0);
            return;
        }
        if (scenario == "report_ownership") {
            subscriber.init(settings);
            subscriber.connect();
            Plugin other;
            other.init(settings);
            CHECK(other.api.connect_fn(other.instance) == -1);
            CHECK(subscriber.api.is_connected_fn(subscriber.instance) == 1);
            IedServer_lockDataModel(server.server);
            IedServer_updateFloatAttributeValue(server.server, server.power, 126.0f);
            IedServer_unlockDataModel(server.server);
            eventually([&] { return subscriber.read({"power"})[0]["value"] == 126.0; });
            CHECK(subscriber.api.disconnect_fn(subscriber.instance) == 0);
            auto ambiguous = settings;
            ambiguous["iec61850"]["reports"].push_back({{"rcb_ref", "SIMLD0/LLN0.BR.brcb01"},
                                                        {"dataset_ref", "SIMLD0/LLN0$measurements"},
                                                        {"integrity_period_ms", 1000}});
            other.init(ambiguous);
            CHECK(other.api.connect_fn(other.instance) == -1);
            CHECK(other.error().find("ambiguous RptId") != std::string::npos);
            subscriber.connect();
            return;
        }
        if (scenario == "report_integrity") {
            settings["iec61850"]["stale_ms"] = 1500;
            subscriber.init(settings);
            subscriber.connect();
            eventually([&] { return subscriber.read({"power"})[0]["source"] == "report"; });
            std::atomic<int> reads{0};
            IedServer_setReadAccessHandler(
                server.server,
                [](LogicalDevice*, LogicalNode*, DataObject*, FunctionalConstraint,
                   ClientConnection, void* state) -> MmsDataAccessError {
                    ++*static_cast<std::atomic<int>*>(state);
                    return DATA_ACCESS_ERROR_SUCCESS;
                },
                &reads);
            const auto initial = subscriber.read({"power"})[0]["report"]["sequence"];
            std::this_thread::sleep_for(std::chrono::milliseconds(1600));
            const auto sample = subscriber.read({"power"})[0];
            CHECK(sample["value"] == 125.5 && sample["quality"] == 0);
            CHECK(sample["report"]["sequence"] != initial);
            CHECK(reads.load() == 0);
            CHECK(subscriber.api.disconnect_fn(subscriber.instance) == 0);
            IedServer_setReadAccessHandler(server.server, nullptr, nullptr);
            return;
        }
        if (scenario == "report_faults" || scenario == "report_queue") {
            if (scenario == "report_faults") settings["timeout_ms"] = 250;
            auto& options = settings["iec61850"];
            options["points"] = Json::array({options["points"][1]});
            options["reports"][0]["gi_on_connect"] = false;
            options["reports"][0]["trigger_options"] = Json::array({"gi"});
            if (scenario == "report_queue") {
                options["report_queue_size"] = 16;

                struct Burst {
                    Server* server;
                    bool sent{false};
                } burst{&server};

                IedServer_setReadAccessHandler(
                    server.server,
                    [](LogicalDevice*, LogicalNode*, DataObject*, FunctionalConstraint,
                       ClientConnection, void* context) -> MmsDataAccessError {
                        auto* burst = static_cast<Burst*>(context);
                        if (!burst->sent) {
                            burst->sent = true;
                            // 此处已持有会话锁；在初读响应前投递完整突发，工作线程尚未启动。
                            for (int i = 0; i < 200; ++i)
                                burst->server->send_report(i, false, -1, false, false, 1, true,
                                                           true);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        return DATA_ACCESS_ERROR_SUCCESS;
                    },
                    &burst);
                subscriber.init(settings);
                const int connected = subscriber.api.connect_fn(subscriber.instance);
                CHECK(connected == 0);
                eventually([&] {
                    auto result = subscriber.read({"status"})[0];
                    return result.contains("report") &&
                           result["report"].value("resync_count", 0) > 0 &&
                           result["report"].value("last_resync_reason", "") ==
                               "report_queue_overflow_or_copy_failure" &&
                           result["report"].value("recovered", false);
                });
                CHECK(subscriber.api.disconnect_fn(subscriber.instance) == 0);
                IedServer_setReadAccessHandler(server.server, nullptr, nullptr);
                return;
            }
            subscriber.init(settings);
            subscriber.connect();
            server.send_report(40, false);
            eventually([&] { return subscriber.read({"status"})[0]["value"] == 0; });
            server.send_report(42, false);
            eventually([&] {
                auto result = subscriber.read({"status"})[0];
                return result["value"] == 1 && result.contains("report") &&
                       result["report"].value("last_resync_reason", "") == "report_sequence_gap";
            });
            server.send_report(43, false, 0, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            CHECK(subscriber.read({"status"})[0]["value"] == 1);
            server.send_report(43, false, 1, false, false, 1, false);
            eventually([&] { return subscriber.read({"status"})[0]["value"] == 0; });
            server.send_report(44, false, 1);
            eventually([&] {
                auto result = subscriber.read({"status"})[0];
                return result["value"] == 1 && result["report"].value("last_resync_reason", "") ==
                                                   "report_segment_missing_or_expired";
            });
            server.send_report(45, false, -1, false, true);
            eventually([&] {
                auto result = subscriber.read({"status"})[0];
                return result["value"] == 1 &&
                       result["report"].value("last_resync_reason", "") == "report_buffer_overflow";
            });
            server.send_report(46, false, 0, true);
            eventually([&] {
                auto result = subscriber.read({"status"})[0];
                return result["value"] == 1 &&
                       result["report"].value("last_resync_reason", "") == "report_segment_timeout";
            });
            server.send_report(47, false, -1, false, false, 2);
            eventually([&] { return subscriber.api.is_connected_fn(subscriber.instance) == 0; });
            CHECK(subscriber.read({"status"})[0]["quality"] == 1);
            CHECK(subscriber.read({"status"})[0]["report"]["last_resync_reason"] ==
                  "report_conf_rev_changed");
            return;
        }
        if (scenario == "report_fallback") {
            settings["iec61850"]["reports"][0]["rcb_ref"] = "SIMLD0/LLN0.RP.missing";
            subscriber.init(settings);
            CHECK(subscriber.api.connect_fn(subscriber.instance) == -1);
            settings["iec61850"]["read_mode"] = "hybrid";
            settings["iec61850"]["reports"][0]["required"] = false;
            settings["poll_interval_ms"] = 50;
        }
        subscriber.init(settings);
        subscriber.connect();
        CHECK(subscriber.read({"power"})[0]["value"] == 125.5);
        IedServer_lockDataModel(server.server);
        IedServer_updateFloatAttributeValue(server.server, server.power, 222.0f);
        IedServer_unlockDataModel(server.server);
        eventually([&] { return subscriber.read({"power"})[0]["value"] == 222.0; });
        if (scenario == "report_reconnect") {
            IedServer_stop(server.server);
            eventually([&] { return subscriber.api.is_connected_fn(subscriber.instance) == 0; });
            CHECK(subscriber.read({"power"})[0]["quality"] == 1);
            server.start();
            subscriber.connect();
            CHECK(subscriber.read({"power"})[0]["value"] == 222.0);
        } else if (scenario != "report_fallback") {
            eventually([&] { return subscriber.read({"power"})[0]["source"] == "report"; });
            IedServer_lockDataModel(server.server);
            IedServer_updateQuality(server.server, server.quality, QUALITY_TEST);
            IedServer_unlockDataModel(server.server);
            eventually([&] { return subscriber.read({"power"})[0]["quality"] == 1; });
            CHECK(subscriber.read({"status"})[0]["value"] == 1);
        } else {
            CHECK(subscriber.read({"power"})[0].contains("report_warning"));
            CHECK(subscriber.api.disconnect_fn(subscriber.instance) == 0);
            settings["iec61850"].erase("datasets");
            settings["iec61850"]["reports"][0]["dataset_ref"] = "SIMLD0/LLN0$missing";
            subscriber.init(settings);
            subscriber.connect();
            CHECK(subscriber.read({"power"})[0]["value"] == 222.0);
            CHECK(subscriber.read({"power"})[0].contains("report_warning"));
        }
        return;
    }
    if (scenario == "concurrent_instances") {
        auto task = [&]() {
            for (int i = 0; i < 5; ++i) {
                Plugin instance;
                instance.init(config(server.port, i % 2 == 0));
                instance.connect();
                CHECK(instance.read({"power", "status"})[0]["value"] == 125.5);
                CHECK(instance.api.disconnect_fn(instance.instance) == 0);
            }
        };
        std::exception_ptr first, second;
        std::thread a([&] {
            try {
                task();
            } catch (...) {
                first = std::current_exception();
            }
        });
        std::thread b([&] {
            try {
                task();
            } catch (...) {
                second = std::current_exception();
            }
        });
        a.join();
        b.join();
        if (first) std::rethrow_exception(first);
        if (second) std::rethrow_exception(second);
        return;
    }
    Plugin p;
    const bool use_dataset =
        scenario == "dataset" || scenario == "quality" || scenario == "partial_access";
    auto value = config(server.port, use_dataset);
    if (scenario == "budget") value["timeout_ms"] = 80;
#ifdef EMS_IEC61850_HANDLER_TEST
    if (scenario == "handler") {
        auto handler = ems::net::handler::create_handler(p.manager, "IEC61850");
        CHECK(handler->initialize(value));
        CHECK(handler->connect());
        std::vector<ems::net::handler::PointValue> samples;
        CHECK(handler->read_batch({"power", "unknown", "status"}, samples) == 2);
        CHECK(samples.size() == 3 && samples[0].value == 125.5 && samples[2].value == 1);
        CHECK(samples[0].details["iec_quality"] == 0);
        CHECK(!handler->write_value("power", 5));
        CHECK(handler->disconnect());
        auto invalid = value;
        invalid["iec61850"]["read_mode"] = "report";
        CHECK(!handler->initialize(invalid));
        CHECK(handler->connect());
        CHECK(handler->read_batch({"power"}, samples) == 1);
        CHECK(handler->disconnect());
        return;
    }
#endif
    if (scenario == "model_validation") {
        value["iec61850"]["points"][0]["mms_type"] = "integer";
        p.init(value);
        CHECK(p.api.connect_fn(p.instance) == -1);
        CHECK(p.error().find("type mismatch") != std::string::npos);
        CHECK(p.api.is_connected_fn(p.instance) == 0);
        value = config(server.port);
        value["iec61850"]["points"][0]["object_ref"] = "SIMLD0/MMXU1.TotW.missing";
        p.init(value);
        CHECK(p.api.connect_fn(p.instance) == -1);
        p.init(config(server.port));
        p.connect();
        return;
    }
    if (scenario == "limits") {
        value["iec61850"]["max_model_nodes"] = 1;
        p.init(value);
        CHECK(p.api.connect_fn(p.instance) == -1);
        CHECK(p.error().find("model ") == 0 &&
              p.error().find("limit exceeded") != std::string::npos);
        value = config(server.port);
        value["iec61850"]["max_model_depth"] = 1;
        p.init(value);
        CHECK(p.api.connect_fn(p.instance) == -1);
        CHECK(p.error().find("model ") == 0 &&
              p.error().find("limit exceeded") != std::string::npos);
        return;
    }
    p.init(value);
    p.connect();
    if (scenario == "scalar" || scenario == "dataset") {
        const auto result = p.read({"status", "power", "unknown", "power", "large", "array"});
        CHECK(result.size() == 6);
        CHECK(result[0]["value"] == 1);
        CHECK(result[1]["value"] == 125.5);
        CHECK(result[1]["timestamp"] == kTimestamp);
        CHECK(result[1]["iec_quality"] == 0);
        CHECK(result[2]["value"].is_null());
        CHECK(result[3] == result[1]);
        CHECK(result[4]["value"] == 42);
        CHECK(result[5]["value"] == 11);
        CHECK(result[1]["metadata_coherent"] == use_dataset);
        CHECK(result[1]["source"] == (use_dataset ? "dataset" : "read"));
        CHECK(result[4]["source"] == "read");
        const auto other = p.read({"unsigned", "double"});
        CHECK(other[0]["value"] == 4294967295.0);
        CHECK(other[1]["value"] == -3.25);
    } else if (scenario == "quality") {
        IedServer_lockDataModel(server.server);
        IedServer_updateQuality(server.server, server.quality, QUALITY_TEST);
        IedServer_updateInt64AttributeValue(server.server, server.large, 9007199254740992LL);
        IedServer_unlockDataModel(server.server);
        auto result = p.read({"power", "large", "status"});
        CHECK(result[0]["quality"] == 1);
        CHECK(result[0]["iec_quality"] == QUALITY_TEST);
        CHECK(result[1]["value"].is_null());
        CHECK(result[1]["raw_integer"] == "9007199254740992");
        CHECK(result[2]["quality"] == 0);
        IedServer_lockDataModel(server.server);
        IedServer_updateQuality(server.server, server.quality, 0);
        IedServer_updateFloatAttributeValue(server.server, server.power,
                                            std::numeric_limits<float>::infinity());
        IedServer_unlockDataModel(server.server);
        result = p.read({"power"});
        CHECK(result[0]["quality"] == 1);
        IedServer_lockDataModel(server.server);
        IedServer_updateInt64AttributeValue(server.server, server.large, -9007199254740991LL);
        IedServer_updateFloatAttributeValue(server.server, server.power, 123.0f);
        MmsValue_setUtcTimeQuality(IedServer_getAttributeValue(server.server, server.timestamp),
                                   0x20);
        IedServer_unlockDataModel(server.server);
        result = p.read({"power", "large"});
        CHECK(result[0]["error"] == "invalid_device_clock");
        CHECK(result[1]["value"] == -9007199254740991.0);
    } else if (scenario == "buffer_retry") {
        const char* codes[] = {"power"};
        char tiny[2];
        CHECK(p.api.read_batch_fn(p.instance, codes, 1, tiny, sizeof(tiny)) == -2);
        IedServer_lockDataModel(server.server);
        IedServer_updateFloatAttributeValue(server.server, server.power, 200.0f);
        IedServer_unlockDataModel(server.server);
        CHECK(p.read({"power"})[0]["value"] == 125.5);
        CHECK(p.read({"power"})[0]["value"] == 200.0);
    } else if (scenario == "reconnect") {
        CHECK(p.read({"power"})[0]["quality"] == 0);
        IedServer_stop(server.server);
        CHECK(p.read({"power"})[0]["quality"] == 1);
        CHECK(p.api.disconnect_fn(p.instance) == 0);
        CHECK(p.api.disconnect_fn(p.instance) == 0);
        server.start();
        p.connect();
        CHECK(p.read({"power"})[0]["value"] == 125.5);
    } else if (scenario == "partial_access") {
        IedServer_setReadAccessHandler(
            server.server,
            [](LogicalDevice*, LogicalNode*, DataObject* object, FunctionalConstraint,
               ClientConnection, void*) -> MmsDataAccessError {
                return std::string(object->name) == "TotW" ? DATA_ACCESS_ERROR_OBJECT_ACCESS_DENIED
                                                           : DATA_ACCESS_ERROR_SUCCESS;
            },
            nullptr);
        const auto result = p.read({"power", "status", "array"});
        CHECK(result[0]["value"].is_null());
        CHECK(result[0]["quality"] == 1);
        CHECK(result[1]["value"] == 1);
        CHECK(result[2]["value"] == 11);
    } else if (scenario == "budget") {
        std::atomic<int> calls{0};
        IedServer_setReadAccessHandler(
            server.server,
            [](LogicalDevice*, LogicalNode*, DataObject*, FunctionalConstraint, ClientConnection,
               void* state) -> MmsDataAccessError {
                ++*static_cast<std::atomic<int>*>(state);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return DATA_ACCESS_ERROR_SUCCESS;
            },
            &calls);
        const auto start = std::chrono::steady_clock::now();
        const auto result = p.read({"power", "status", "large"});
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500));
        CHECK(calls.load() == 1);
        CHECK(result[0]["quality"] == 1 && result[1]["quality"] == 1 && result[2]["quality"] == 1);
        // 等待服务端回调退出后再销毁其借用的计数器。
        IedServer_stop(server.server);
    } else if (scenario == "settings") {
        const Json wrapped{{"protocol", "IEC61850"}, {"settings", value.dump()}};
        CHECK(p.api.disconnect_fn(p.instance) == 0);
        p.init(wrapped);
        p.connect();
        CHECK(p.read({"status"})[0]["value"] == 1);
        auto bad = value;
        bad["iec61850"]["points"][0]["fc"] = "bad";
        CHECK(p.api.init_fn(p.instance, bad.dump().c_str()) == -1);
        CHECK(p.read({"power"})[0]["value"] == 125.5);
    } else {
        throw std::runtime_error("unknown scenario");
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        run(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
