#include "net/handler/plugin_handler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <stdexcept>

#include "net/protocol/plugin_manager.h"

namespace ems::net::handler {
namespace {
std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

json normalized_config(json config, const std::string& protocol) {
    if (!config.is_object()) throw std::invalid_argument("Config must be a JSON object");
    if (config.contains("settings")) {
        auto settings = config.at("settings");
        if (settings.is_string())
            settings = settings.get<std::string>().empty()
                           ? json::object()
                           : json::parse(settings.get<std::string>());
        if (!settings.is_object()) throw std::invalid_argument("settings must be a JSON object");
        for (auto it = settings.begin(); it != settings.end(); ++it)
            if (!config.contains(it.key())) config[it.key()] = it.value();
    }
    if (config.contains("protocol") && config.at("protocol") != protocol)
        throw std::invalid_argument("Config protocol does not match handler");
    config["protocol"] = protocol;
    const auto role = config.value("local_role", config.value("role", std::string("client")));
    if (role != "client" && role != "server" && role != "server_readonly")
        throw std::invalid_argument("Invalid handler role");
    config["local_role"] = role == "client" ? "client" : "server";
    config["role"] = role == "client" ? "client" : "server_readonly";
    if (protocol == "ModbusTCP" || protocol == "ModbusRTU") {
        const std::string transport = protocol == "ModbusTCP" ? "tcp" : "rtu";
        if (config.contains("transport") && config.at("transport") != transport)
            throw std::invalid_argument("Modbus transport does not match handler");
        config["transport"] = transport;
    }
    return config;
}
}  // namespace

class PluginHandler::Impl {
   public:
    collect::PluginManager& manager;
    const std::string protocol;
    mutable std::mutex mutex;
    void* instance{nullptr};
    collect::PluginManager::PluginFunctions functions;
    std::string error;

    Impl(collect::PluginManager& owner, std::string name)
        : manager(owner), protocol(std::move(name)) {
        if (protocol.empty()) throw std::invalid_argument("Protocol name is empty");
    }

    ~Impl() {
        // 释放失败由管理器继续持有资源，不能提前卸载可能还有回调的库。
        if (instance) manager.release(instance);
    }

    void plugin_error() {
        const char* text = functions.last_error_fn(instance);
        error = text ? text : "Unknown plugin error";
    }
};

PluginHandler::PluginHandler(collect::PluginManager& manager, const std::string& protocol_name)
    : impl_(std::make_unique<Impl>(manager, protocol_name)) {}

PluginHandler::~PluginHandler() = default;

bool PluginHandler::initialize(const json& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (impl_->instance && impl_->functions.is_connected_fn(impl_->instance) == 1)
            throw std::invalid_argument("Disconnect before reconfiguring handler");
        const auto text = normalized_config(config, impl_->protocol).dump();
        // 新配置使用临时实例验证；失败不破坏之前已校验的配置。
        auto release = [&](void* instance) { impl_->manager.release(instance); };
        std::unique_ptr<void, decltype(release)> candidate(impl_->manager.load(impl_->protocol),
                                                           release);
        if (!candidate) throw std::runtime_error(impl_->manager.last_error());
        const auto functions = impl_->manager.get_instance_functions(candidate.get());
        if (functions.init_fn(candidate.get(), text.c_str()) != 0) {
            const char* error = functions.last_error_fn(candidate.get());
            throw std::runtime_error(error ? error : "Plugin initialization failed");
        }
        if (impl_->instance && !impl_->manager.release(impl_->instance))
            throw std::runtime_error(impl_->manager.last_error());
        impl_->instance = candidate.release();
        impl_->functions = functions;
        impl_->error.clear();
        return true;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return false;
    }
}

bool PluginHandler::connect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->instance) {
        impl_->error = "Initialize handler before connecting";
        return false;
    }
    if (impl_->functions.connect_fn(impl_->instance) != 0) {
        impl_->plugin_error();
        return false;
    }
    impl_->error.clear();
    return true;
}

bool PluginHandler::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->instance && impl_->functions.disconnect_fn(impl_->instance) != 0) {
        impl_->plugin_error();
        return false;
    }
    impl_->error.clear();
    return true;
}

bool PluginHandler::is_connected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->instance && impl_->functions.is_connected_fn(impl_->instance) == 1;
}

int PluginHandler::read_batch(const std::vector<std::string>& codes,
                              std::vector<PointValue>& results) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    results.clear();
    try {
        if (!impl_->instance) throw std::runtime_error("Handler has not been initialized");
        if (codes.size() > 65536) throw std::invalid_argument("Batch too large");
        std::vector<const char*> pointers;
        pointers.reserve(codes.size());
        for (const auto& code : codes) pointers.push_back(code.c_str());
        std::vector<char> output(EMS_PLUGIN_RESULT_BUF_SIZE);
        constexpr std::size_t max_bytes = 8 * 1024 * 1024;
        int count;
        do {
            count = impl_->functions.read_batch_fn(impl_->instance, pointers.data(),
                                                   static_cast<int>(pointers.size()), output.data(),
                                                   static_cast<int>(output.size()));
            if (count != EMS_PLUGIN_BUFFER_TOO_SMALL || output.size() == max_bytes) break;
            output.resize(std::min(output.size() * 2, max_bytes));
        } while (true);
        if (count < 0) {
            impl_->plugin_error();
            return -1;
        }
        const auto terminator = std::find(output.begin(), output.end(), '\0');
        if (terminator == output.end())
            throw std::runtime_error("Plugin result is not NUL terminated");
        const auto samples = json::parse(output.begin(), terminator);
        if (!samples.is_array() || samples.size() != codes.size())
            throw std::runtime_error("Plugin result size does not match batch");
        int valid = 0;
        std::vector<PointValue> parsed;
        parsed.reserve(codes.size());
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto& item = samples[i];
            if (!item.is_object() || item.value("code", std::string()) != codes[i])
                throw std::runtime_error("Plugin result order does not match batch");
            PointValue point;
            point.code = codes[i];
            point.quality = item.value("quality", 1);
            const auto value = item.find("value");
            if (value != item.end() && value->is_number())
                point.value = value->get<double>();
            else if (value != item.end() && value->is_boolean())
                point.value = value->get<bool>() ? 1 : 0;
            else if (point.quality == 0)
                point.quality = 3;
            if (!std::isfinite(point.value)) {
                point.value = 0;
                point.quality = 3;
            }
            if (point.quality < 0) point.quality = 1;
            point.timestamp = item.value("timestamp", now_ms());
            point.rawValue = item.value("rawValue", std::int64_t{0});
            point.details = item;
            if (point.quality == 0) ++valid;
            parsed.push_back(std::move(point));
        }
        if (valid != count) throw std::runtime_error("Plugin result count does not match quality");
        results = std::move(parsed);
        impl_->error.clear();
        return valid;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

bool PluginHandler::write_value(const std::string& code, double value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->instance) {
        impl_->error = "Handler has not been initialized";
        return false;
    }
    if (impl_->functions.write_fn(impl_->instance, code.c_str(), value) != 0) {
        impl_->plugin_error();
        return false;
    }
    impl_->error.clear();
    return true;
}

std::string PluginHandler::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

std::string PluginHandler::protocol_name() const { return impl_->protocol; }
}  // namespace ems::net::handler
