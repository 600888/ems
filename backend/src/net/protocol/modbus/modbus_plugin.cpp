/**
 * @file modbus_plugin.cpp
 * @brief Modbus 协议插件 C 导出符号
 *
 * 基于 ModbusClientHandler 提供完整的 Modbus TCP/RTU 采集能力。
 * 编译为 libproto_modbus.so，通过 dlopen/dlsym 动态加载。
 */
#include "modbus_handler.h"
#include "net/protocol/common/protocol_plugin_api.h"

#include <cstring>
#include <nlohmann/json.hpp>

using Handler = ems::net::device::ModbusClientHandler;
using ValueType = ems::net::device::ValueType;
using json = nlohmann::json;

static constexpr EmPluginInfo MODBUS_PLUGIN_INFO = {
    .api_version   = EMS_PLUGIN_API_VERSION,
    .protocol_name = "ModbusTCP",
    .description   = "Modbus TCP/RTU protocol plugin for EMS data acquisition",
    .version       = "1.0.0"
};

static inline Handler* H(void* inst) { return static_cast<Handler*>(inst); }

static std::unordered_map<std::string, ValueType> json_to_config(const json& j) {
    std::unordered_map<std::string, ValueType> cfg;
    if (j.contains("ip"))               cfg["ip"]               = j["ip"].get<std::string>();
    if (j.contains("host"))             cfg["host"]             = j["host"].get<std::string>();
    if (j.contains("port"))             cfg["port"]             = j["port"].get<int>();
    if (j.contains("serial_port"))      cfg["serial_port"]      = j["serial_port"].get<std::string>();
    if (j.contains("baudrate"))         cfg["baudrate"]         = j["baudrate"].get<int>();
    if (j.contains("parity"))           cfg["parity"]           = j["parity"].get<std::string>();
    if (j.contains("stopbits"))         cfg["stopbits"]         = j["stopbits"].get<int>();
    if (j.contains("slave_id"))         cfg["slave_id"]         = j["slave_id"].get<int>();
    if (j.contains("timeout_ms"))       cfg["response_timeout"] = j["timeout_ms"].get<int>();
    if (j.contains("transport") && j["transport"] == "rtu")     cfg["protocol_type"] = 0;
    return cfg;
}

extern "C" {

const EmPluginInfo* ems_plugin_info() { return &MODBUS_PLUGIN_INFO; }

void* ems_plugin_create() { return static_cast<void*>(new Handler()); }

void ems_plugin_destroy(void* inst) { delete H(inst); }

int ems_plugin_init(void* inst, const char* jc) {
    try { H(inst)->initialize(json_to_config(json::parse(jc))); return 0; } catch (...) { return -1; }
}

int ems_plugin_connect(void* inst) { return inst ? (H(inst)->connect() ? 0 : -1) : -1; }

int ems_plugin_disconnect(void* inst) { if (inst) H(inst)->disconnect(); return 0; }

int ems_plugin_read_batch(void* inst, const char* codes[], int n, char* out, int sz) {
    if (!inst || !codes || n <= 0 || !out || sz <= 0) return -1;
    std::vector<std::string> c; c.reserve(n);
    for (int i = 0; i < n; ++i) if (codes[i]) c.emplace_back(codes[i]);
    if (c.empty()) return 0;
    std::vector<Handler::PointValue> results;
    int ok = H(inst)->read_batch(c, results);
    try {
        json j = json::array();
        for (const auto& pv : results) {
            j.push_back({{"code", std::get<0>(pv)}, {"value", std::get<1>(pv)},
                         {"raw_value", std::get<2>(pv)}, {"quality", std::get<3>(pv)},
                         {"timestamp", std::get<4>(pv)}});
        }
        auto s = j.dump();
        if (s.size() + 1 > static_cast<size_t>(sz)) return -1;
        std::strncpy(out, s.c_str(), sz - 1); out[sz - 1] = '\0';
        return ok;
    } catch (...) { return -1; }
}

int ems_plugin_write(void* inst, const char* pc, double value) {
    if (!inst || !pc) return -1;
    auto pp = std::make_shared<ems::net::device::ProtocolPoint>();
    auto parsed = Handler::parse_point_code(pc);
    pp->rtu_addr = parsed.slaveId; pp->func_code = parsed.funcCode; pp->address = parsed.address;
    return H(inst)->writeValue(pp, static_cast<int>(std::round(value))) ? 0 : -1;
}

int ems_plugin_is_connected(void* inst) { return inst ? (H(inst)->isConnected() ? 1 : 0) : 0; }

const char* ems_plugin_last_error(void* inst) {
    if (!inst) return "No instance";
    static thread_local std::string c; c = H(inst)->last_error(); return c.c_str();
}

} // extern "C"
