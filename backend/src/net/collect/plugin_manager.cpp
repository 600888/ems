#include "plugin_manager.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "net/protocol/dynamic_library.h"

namespace ems::net::collect {
namespace fs = std::filesystem;

namespace {
struct LibraryEntry {
    protocol::DynamicLibrary library;
    PluginManager::PluginFunctions functions;
    std::once_flag once;
    std::string error;
    bool ready{false};

    void initialize(const std::string& path) {
        std::call_once(once, [&] {
            if (!library.open(path)) {
                error = library.last_error();
                return;
            }
            auto resolve = [&](auto& function, const char* name) {
                function = reinterpret_cast<std::decay_t<decltype(function)>>(library.symbol(name));
                if (!function) error = library.last_error();
                return function != nullptr;
            };
            if (!resolve(functions.info_fn, EMS_PLUGIN_INFO_SYMBOL) ||
                !resolve(functions.create_fn, EMS_PLUGIN_CREATE_SYMBOL) ||
                !resolve(functions.destroy_fn, EMS_PLUGIN_DESTROY_SYMBOL) ||
                !resolve(functions.init_fn, EMS_PLUGIN_INIT_SYMBOL) ||
                !resolve(functions.connect_fn, EMS_PLUGIN_CONNECT_SYMBOL) ||
                !resolve(functions.disconnect_fn, EMS_PLUGIN_DISCONNECT_SYMBOL) ||
                !resolve(functions.read_batch_fn, EMS_PLUGIN_READ_BATCH_SYMBOL) ||
                !resolve(functions.write_fn, EMS_PLUGIN_WRITE_SYMBOL) ||
                !resolve(functions.is_connected_fn, EMS_PLUGIN_IS_CONNECTED_SYMBOL) ||
                !resolve(functions.last_error_fn, EMS_PLUGIN_LAST_ERROR_SYMBOL))
                return;
            const auto* info = functions.info_fn();
            if (!info || info->api_version != EMS_PLUGIN_API_VERSION) {
                error = "Plugin API version mismatch";
                return;
            }
            ready = true;
        });
    }
};

struct InstanceRecord {
    std::shared_ptr<LibraryEntry> lease;
    std::string protocol;
};
}  // namespace

class PluginManager::Impl {
   public:
    mutable std::mutex mutex;
    std::string directory{"./lib"};
    std::unordered_map<std::string, std::string> mappings;
    std::unordered_map<std::string, std::weak_ptr<LibraryEntry>> libraries;
    std::unordered_map<void*, InstanceRecord> instances;
    std::string error;

    void set_error(std::string message) {
        std::lock_guard<std::mutex> lock(mutex);
        error = std::move(message);
    }
};

const std::unordered_map<std::string, std::string>& PluginManager::default_lib_map() {
#ifdef _WIN32
    constexpr const char* prefix = "";
    constexpr const char* suffix = ".dll";
#else
    constexpr const char* prefix = "lib";
    constexpr const char* suffix = ".so";
#endif
    static const std::unordered_map<std::string, std::string> map = {
        {"ModbusTCP", std::string(prefix) + "proto_modbus" + suffix},
        {"ModbusRTU", std::string(prefix) + "proto_modbus" + suffix},
        {"DLT645", std::string(prefix) + "proto_dlt645" + suffix},
        {"IEC104", std::string(prefix) + "proto_iec104" + suffix},
        {"IEC61850", std::string(prefix) + "proto_iec61850" + suffix}};
    return map;
}

PluginManager::PluginManager() : impl_(std::make_unique<Impl>()) {}

PluginManager::~PluginManager() {
    // 调用方须先 join 通道；停止失败的实例和库保留至进程终止。
    std::vector<void*> instances;
    for (const auto& item : impl_->instances) instances.push_back(item.first);
    for (void* instance : instances) release(instance);
    for (auto& item : impl_->instances) {
        auto* pinned = new InstanceRecord(std::move(item.second));
        (void)pinned;
    }
}

void PluginManager::set_plugin_dir(const std::string& dir) {
    const auto absolute = fs::absolute(fs::u8path(dir)).lexically_normal().u8string();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->directory = absolute;
}

std::string PluginManager::plugin_dir() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->directory;
}

void PluginManager::register_mapping(const std::string& name, const std::string& file) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->mappings[name] = file;
}

void* PluginManager::load(const std::string& name) {
    std::shared_ptr<LibraryEntry> lease;
    std::string path;
    try {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            const auto custom = impl_->mappings.find(name);
            const auto fallback = default_lib_map().find(name);
            if (custom == impl_->mappings.end() && fallback == default_lib_map().end()) {
                impl_->error = "Unknown protocol: " + name;
                return nullptr;
            }
            const auto& file = custom != impl_->mappings.end() ? custom->second : fallback->second;
            const auto root = fs::weakly_canonical(fs::u8path(impl_->directory));
            const auto library_path = fs::weakly_canonical(root / fs::u8path(file));
            const auto relative = library_path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
                impl_->error = "Plugin path is outside the plugin directory";
                return nullptr;
            }
            path = library_path.u8string();
#ifdef _WIN32
            auto key = library_path.native();
            for (auto& c : key) c = static_cast<wchar_t>(std::towlower(c));
            const auto cache_key = fs::path(key).u8string();
#else
            const auto& cache_key = path;
#endif
            lease = impl_->libraries[cache_key].lock();
            if (!lease) {
                lease = std::make_shared<LibraryEntry>();
                impl_->libraries[cache_key] = lease;
            }
        }
        lease->initialize(path);
        if (!lease->ready) {
            impl_->set_error("Cannot load " + path + ": " + lease->error);
            return nullptr;
        }
        void* instance = lease->functions.create_fn();
        if (!instance) {
            impl_->set_error("Plugin create returned null: " + name);
            return nullptr;
        }
        try {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            const auto inserted = impl_->instances.emplace(instance, InstanceRecord{lease, name});
            if (!inserted.second) {
                impl_->error = "Plugin factory returned an existing instance";
                return nullptr;
            }
            impl_->error.clear();
        } catch (...) {
            lease->functions.destroy_fn(instance);
            throw;
        }
        return instance;
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
    } catch (...) {
        impl_->set_error("Plugin load raised an unknown exception");
    }
    return nullptr;
}

bool PluginManager::release(void* instance) {
    InstanceRecord record;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->instances.find(instance);
        if (it == impl_->instances.end()) return false;
        record = std::move(it->second);
        impl_->instances.erase(it);
    }
    try {
        if (record.lease->functions.disconnect_fn(instance) != 0)
            throw std::runtime_error("Plugin stop failed; keeping its instance and library");
        record.lease->functions.destroy_fn(instance);
        return true;
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
    } catch (...) {
        impl_->set_error("Plugin release raised an unknown exception");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->instances.emplace(instance, std::move(record));
    return false;
}

void PluginManager::unload(const std::string& name) {
    void* instance = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& item : impl_->instances) {
            if (item.second.protocol == name) {
                instance = item.first;
                break;
            }
        }
    }
    if (instance) release(instance);
}

bool PluginManager::is_loaded(const std::string& name) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& item : impl_->instances) {
        if (item.second.protocol == name) return true;
    }
    return false;
}

std::size_t PluginManager::loaded_library_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::size_t count = 0;
    for (const auto& item : impl_->libraries) {
        if (!item.second.expired()) ++count;
    }
    return count;
}

std::vector<std::string> PluginManager::loaded_protocols() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> names;
    for (const auto& item : impl_->instances) names.push_back(item.second.protocol);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

const EmPluginInfo* PluginManager::get_plugin_info(void* instance) const {
    std::shared_ptr<LibraryEntry> lease;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto it = impl_->instances.find(instance);
        if (it == impl_->instances.end()) return nullptr;
        lease = it->second.lease;
    }
    return lease->functions.info_fn();
}

PluginManager::PluginFunctions PluginManager::get_functions(const std::string& name) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::shared_ptr<LibraryEntry> lease;
    for (const auto& item : impl_->instances) {
        if (item.second.protocol != name) continue;
        if (lease && lease != item.second.lease) return {};
        lease = item.second.lease;
    }
    return lease ? lease->functions : PluginFunctions{};
}

PluginManager::PluginFunctions PluginManager::get_instance_functions(void* instance) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto it = impl_->instances.find(instance);
    return it == impl_->instances.end() ? PluginFunctions{} : it->second.lease->functions;
}

std::string PluginManager::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

}  // namespace ems::net::collect
