/**
 * @file plugin_manager.cpp
 * @brief 协议插件管理器实现
 */
#include "plugin_manager.h"
#include <dlfcn.h>
#include <cstring>
#include <algorithm>
#include <type_traits>
#include <filesystem>

namespace fs = std::filesystem;

namespace ems {
namespace net {
namespace collect {

// ============================================================
// 默认协议 -> 动态库文件名 映射表
// ============================================================

const std::unordered_map<std::string, std::string>& PluginManager::default_lib_map() {
    static const std::unordered_map<std::string, std::string> map = {
        {"ModbusTCP",      "libproto_modbus.so"},
        {"ModbusRTU",      "libproto_modbus.so"},
        {"DLT645",         "libproto_dlt645.so"},
        {"IEC104",         "libproto_iec104.so"},
        {"IEC61850",       "libproto_iec61850.so"},
    };
    return map;
}

// ============================================================
// 构造 / 析构
// ============================================================

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    // 卸载所有仍有引用的插件
    for (auto& [name, entry] : loaded_plugins_) {
        if (entry.dl_handle && entry.ref_count > 0) {
            dlclose(entry.dl_handle);
        }
    }
    loaded_plugins_.clear();
}

// ============================================================
// 配置
// ============================================================

void PluginManager::set_plugin_dir(const std::string& dir) {
    std::lock_guard lock(mutex_);
    plugin_dir_ = dir;
}

void PluginManager::register_mapping(const std::string& protocol_name, const std::string& lib_name) {
    std::lock_guard lock(mutex_);
    custom_mappings_[protocol_name] = lib_name;
}

// ============================================================
// 核心加载逻辑
// ============================================================

void* PluginManager::load(const std::string& protocol_name) {
    std::lock_guard lock(mutex_);

    // 1. 检查是否已加载
    auto it = loaded_plugins_.find(protocol_name);
    if (it != loaded_plugins_.end()) {
        it->second.ref_count++;
        return it->second.fns.create_fn();
    }

    // 2. 解析动态库路径
    auto lib_path = resolve_lib_path(protocol_name);
    if (lib_path.empty()) {
        last_err_ = "Unknown protocol: " + protocol_name;
        return nullptr;
    }

    // 3. dlopen 加载
    auto* handle = dlopen(lib_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        last_err_ = "dlopen failed for " + lib_path + ": " + (dlerror() ? dlerror() : "unknown error");
        return nullptr;
    }

    // 4. 解析符号
    PluginEntry entry;
    entry.dl_handle = handle;
    if (!load_symbols(handle, entry.fns)) {
        dlclose(handle);
        return nullptr;
    }

    // 5. 获取插件信息（校验 API 版本）
    auto* info = entry.fns.info_fn();
    if (!info || info->api_version != EMS_PLUGIN_API_VERSION) {
        last_err_ = "Plugin API version mismatch for " + protocol_name;
        dlclose(handle);
        return nullptr;
    }

    // 6. 缓存
    entry.ref_count = 1;
    loaded_plugins_[protocol_name] = std::move(entry);

    // 7. 创建实例返回
    return loaded_plugins_[protocol_name].fns.create_fn();
}

void PluginManager::unload(const std::string& protocol_name) {
    std::lock_guard lock(mutex_);

    auto it = loaded_plugins_.find(protocol_name);
    if (it == loaded_plugins_.end()) return;

    it->second.ref_count--;
    if (it->second.ref_count <= 0) {
        if (it->second.dl_handle) {
            dlclose(it->second.dl_handle);
        }
        loaded_plugins_.erase(it);
    }
}

// ============================================================
// 查询
// ============================================================

bool PluginManager::is_loaded(const std::string& protocol_name) const {
    std::lock_guard lock(mutex_);
    return loaded_plugins_.find(protocol_name) != loaded_plugins_.end();
}

std::vector<std::string> PluginManager::loaded_protocols() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> protocols;
    protocols.reserve(loaded_plugins_.size());
    for (const auto& [name, _] : loaded_plugins_) {
        protocols.push_back(name);
    }
    return protocols;
}

const EmPluginInfo* PluginManager::get_plugin_info(void* /*instance*/) const {
    // instance 由具体插件管理，这里需要遍历查找
    // 实际使用中直接从 fns info_fn 获取
    return nullptr;
}

PluginManager::PluginFunctions PluginManager::get_functions(const std::string& protocol_name) const {
    std::lock_guard lock(mutex_);
    auto it = loaded_plugins_.find(protocol_name);
    if (it != loaded_plugins_.end()) {
        return it->second.fns;
    }
    return {};
}

// ============================================================
// 私有辅助方法
// ============================================================

std::string PluginManager::resolve_lib_path(const std::string& protocol_name) const {
    // 1. 检查自定义映射
    auto cm = custom_mappings_.find(protocol_name);
    if (cm != custom_mappings_.end()) {
        return (fs::path(plugin_dir_) / cm->second).string();
    }

    // 2. 检查默认映射
    auto& default_map = default_lib_map();
    auto dm = default_map.find(protocol_name);
    if (dm != default_map.end()) {
        return (fs::path(plugin_dir_) / dm->second).string();
    }

    return {};
}

bool PluginManager::load_symbols(void* handle, PluginFunctions& fns) {
    auto resolve_fn = [&](auto& func_ptr, const char* symbol) -> bool {
        using FuncType = std::remove_reference_t<decltype(func_ptr)>;
        dlerror();
        func_ptr = reinterpret_cast<FuncType>(dlsym(handle, symbol));
        const char* err = dlerror();
        if (err) {
            last_err_ = "dlsym(" + std::string(symbol) + ") failed: " + std::string(err);
            return false;
        }
        return true;
    };

    return resolve_fn(fns.info_fn,          EMS_PLUGIN_INFO_SYMBOL)
        && resolve_fn(fns.create_fn,        EMS_PLUGIN_CREATE_SYMBOL)
        && resolve_fn(fns.destroy_fn,       EMS_PLUGIN_DESTROY_SYMBOL)
        && resolve_fn(fns.init_fn,          EMS_PLUGIN_INIT_SYMBOL)
        && resolve_fn(fns.connect_fn,       EMS_PLUGIN_CONNECT_SYMBOL)
        && resolve_fn(fns.disconnect_fn,    EMS_PLUGIN_DISCONNECT_SYMBOL)
        && resolve_fn(fns.read_batch_fn,    EMS_PLUGIN_READ_BATCH_SYMBOL)
        && resolve_fn(fns.write_fn,         EMS_PLUGIN_WRITE_SYMBOL)
        && resolve_fn(fns.is_connected_fn,  EMS_PLUGIN_IS_CONNECTED_SYMBOL)
        && resolve_fn(fns.last_error_fn,    EMS_PLUGIN_LAST_ERROR_SYMBOL);
}

} // namespace collect
} // namespace net
} // namespace ems
