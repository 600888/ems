/**
 * @file modbus_plugin.cpp
 * @brief Modbus 协议插件 C 导出符号
 *
 * 基于 ModbusClientHandler 提供完整的 Modbus TCP/RTU 采集能力。
 * 编译为 libproto_modbus.so，通过 dlopen/dlsym 动态加载。
 */
#include <spdlog/fmt/bundled/format.h>

#include <cstring>
#include <nlohmann/json.hpp>

#include "modbus_handler.h"
#include "net/log.h"
#include "net/protocol/protocol_plugin_api.h"

using Handler = ems::net::device::ModbusClientHandler;
using ValueType = ems::net::device::ValueType;
using json = nlohmann::json;

static constexpr EmPluginInfo MODBUS_PLUGIN_INFO = {
    .api_version = EMS_PLUGIN_API_VERSION,
    .protocol_name = "ModbusTCP",
    .description = "Modbus TCP/RTU protocol plugin for EMS data acquisition",
    .version = "1.0.0"};

static inline Handler* H(void* inst) { return static_cast<Handler*>(inst); }

static std::unordered_map<std::string, ValueType> json_to_config(const json& j) {
    std::unordered_map<std::string, ValueType> cfg;
    if (j.contains("ip")) cfg["ip"] = j["ip"].get<std::string>();
    if (j.contains("host")) cfg["host"] = j["host"].get<std::string>();
    if (j.contains("port")) cfg["port"] = j["port"].get<int>();
    if (j.contains("serial_port")) cfg["serial_port"] = j["serial_port"].get<std::string>();
    if (j.contains("baudrate")) cfg["baudrate"] = j["baudrate"].get<int>();
    if (j.contains("parity")) cfg["parity"] = j["parity"].get<std::string>();
    if (j.contains("stopbits")) cfg["stopbits"] = j["stopbits"].get<int>();
    if (j.contains("slave_id")) cfg["slave_id"] = j["slave_id"].get<int>();
    if (j.contains("timeout_ms")) cfg["response_timeout"] = j["timeout_ms"].get<int>();
    if (j.contains("transport") && j["transport"] == "rtu") cfg["protocol_type"] = 0;

    // 兼容 ChannelThread 发的 connection="ip:port" 格式
    if (!j.contains("ip") && j.contains("connection")) {
        std::string conn = j["connection"].get<std::string>();
        auto colon = conn.rfind(':');
        if (colon != std::string::npos) {
            cfg["ip"] = conn.substr(0, colon);
            cfg["port"] = std::stoi(conn.substr(colon + 1));
        } else {
            cfg["ip"] = conn;
        }
    }
    return cfg;
}

extern "C" {

const EmPluginInfo* ems_plugin_info() { return &MODBUS_PLUGIN_INFO; }

void* ems_plugin_create() { return static_cast<void*>(new Handler()); }

void ems_plugin_destroy(void* inst) { delete H(inst); }

int ems_plugin_init(void* inst, const char* jc) {
    try {
        LOG_DEBUG(fmt::format("Modbus插件初始化: config={}", jc).c_str());
        H(inst)->initialize(json_to_config(json::parse(jc)));
        LOG_INFO("Modbus插件初始化成功");
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("Modbus插件初始化异常: {}", e.what()).c_str());
        return -1;
    } catch (...) {
        LOG_ERROR("Modbus插件初始化未知异常");
        return -1;
    }
}

int ems_plugin_connect(void* inst) {
    if (!inst) return -1;
    bool ok = H(inst)->connect();
    LOG_INFO(fmt::format("Modbus插件连接: {}", ok ? "成功" : "失败").c_str());
    return ok ? 0 : -1;
}

int ems_plugin_disconnect(void* inst) {
    if (inst) {
        LOG_INFO("Modbus插件断开连接");
        H(inst)->disconnect();
    }
    return 0;
}

int ems_plugin_read_batch(void* inst, const char* codes[], int n, char* out, int sz) {
    if (!inst || !codes || n <= 0 || !out || sz <= 0) return -1;
    std::vector<std::string> c;
    c.reserve(n);
    for (int i = 0; i < n; ++i)
        if (codes[i]) c.emplace_back(codes[i]);
    if (c.empty()) return 0;
    LOG_DEBUG(fmt::format("Modbus插件批量读取: {}个测点", c.size()).c_str());
    std::vector<Handler::PointValue> results;
    int ok = H(inst)->read_batch(c, results);
    LOG_DEBUG(fmt::format("Modbus插件批量读取完成: 成功{}点", ok).c_str());
    try {
        json j = json::array();
        for (const auto& pv : results) {
            j.push_back({{"code", std::get<0>(pv)},
                         {"value", std::get<1>(pv)},
                         {"raw_value", std::get<2>(pv)},
                         {"quality", std::get<3>(pv)},
                         {"timestamp", std::get<4>(pv)}});
        }
        auto s = j.dump();
        if (s.size() + 1 > static_cast<size_t>(sz)) {
            LOG_ERROR(fmt::format("Modbus插件结果缓冲区不足: need={}, have={}", s.size() + 1, sz)
                          .c_str());
            return -1;
        }
        std::strncpy(out, s.c_str(), sz - 1);
        out[sz - 1] = '\0';
        return ok;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("Modbus插件序列化结果异常: {}", e.what()).c_str());
        return -1;
    } catch (...) {
        LOG_ERROR("Modbus插件序列化结果未知异常");
        return -1;
    }
}

int ems_plugin_write(void* inst, const char* pc, double value) {
    if (!inst || !pc) return -1;
    LOG_INFO(fmt::format("Modbus插件写入: pointCode={}, value={}", pc, value).c_str());
    auto pp = std::make_shared<ems::net::device::ProtocolPoint>();
    auto parsed = Handler::parse_point_code(pc);
    pp->rtu_addr = parsed.slaveId;
    pp->func_code = parsed.funcCode;
    pp->address = parsed.address;
    bool ok = H(inst)->writeValue(pp, static_cast<int>(std::round(value)));
    if (!ok)
        LOG_ERROR(fmt::format("Modbus插件写入失败: pointCode={}, value={}", pc, value).c_str());
    return ok ? 0 : -1;
}

int ems_plugin_is_connected(void* inst) {
    bool connected = inst && H(inst)->isConnected();
    return connected ? 1 : 0;
}

const char* ems_plugin_last_error(void* inst) {
    if (!inst) return "No instance";
    static thread_local std::string c;
    c = H(inst)->last_error();
    return c.c_str();
}

}  // extern "C"
