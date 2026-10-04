#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "net/protocol/iec104_protocol.h"
#include "net/protocol/protocol_plugin_api.h"

namespace {
using ems::net::protocol::Iec104Protocol;

const EmPluginInfo kInfo = {EMS_PLUGIN_API_VERSION, "IEC104",
                            "IEC104 PIMPL event client and read-only server", "1.0.0"};

struct Instance {
    Iec104Protocol protocol;
    std::mutex mutex;
    std::array<char, 1024> error{};
    std::string pending;
    std::vector<std::string> pending_codes;
    int pending_count{0};

    void set_error(const char* text) noexcept {
        std::snprintf(error.data(), error.size(), "%s", text ? text : "Unknown plugin error");
    }
};

template <typename Operation>
int invoke(void* opaque, Operation operation) noexcept {
    if (!opaque) return -1;
    auto* instance = static_cast<Instance*>(opaque);
    try {
        std::lock_guard<std::mutex> lock(instance->mutex);
        instance->error[0] = '\0';
        const int result = operation(*instance);
        if (result < 0 && !instance->error[0])
            instance->set_error(instance->protocol.last_error().c_str());
        return result;
    } catch (const std::exception& error) {
        instance->set_error(error.what());
    } catch (...) {
        instance->set_error("Unknown exception at plugin boundary");
    }
    return -1;
}
}  // namespace

extern "C" {

EMS_PLUGIN_EXPORT const EmPluginInfo* EMS_PLUGIN_CALL ems_plugin_info(void) { return &kInfo; }

EMS_PLUGIN_EXPORT void* EMS_PLUGIN_CALL ems_plugin_create(void) {
    try {
        return new Instance();
    } catch (...) {
        return nullptr;
    }
}

EMS_PLUGIN_EXPORT void EMS_PLUGIN_CALL ems_plugin_destroy(void* opaque) {
    try {
        delete static_cast<Instance*>(opaque);
    } catch (...) {
    }
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_init(void* opaque, const char* config) {
    return invoke(opaque, [&](Instance& instance) {
        if (!config) {
            instance.set_error("Config is null");
            return -1;
        }
        instance.pending.clear();
        return instance.protocol.initialize(config);
    });
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_connect(void* opaque) {
    return invoke(opaque, [](Instance& instance) { return instance.protocol.connect(); });
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_disconnect(void* opaque) {
    return invoke(opaque, [](Instance& instance) {
        instance.pending.clear();
        return instance.protocol.disconnect();
    });
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_read_batch(void* opaque, const char* const codes[],
                                                            int count, char* output, int capacity) {
    if (output && capacity > 0) output[0] = '\0';
    return invoke(opaque, [&](Instance& instance) {
        if (count < 0 || count > 65536 || (!codes && count) || !output || capacity <= 0) {
            instance.set_error("Invalid batch arguments");
            return -1;
        }
        std::vector<std::string> points;
        points.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (!codes[i]) {
                instance.set_error("Point code is null");
                return -1;
            }
            points.emplace_back(codes[i]);
        }
        if (instance.pending.empty() || instance.pending_codes != points) {
            instance.pending.clear();
            instance.pending_count = instance.protocol.read_batch(points, instance.pending);
            if (instance.pending_count < 0) return -1;
            instance.pending_codes = points;
        }
        if (instance.pending.size() + 1 > static_cast<std::size_t>(capacity)) {
            std::snprintf(instance.error.data(), instance.error.size(),
                          "Result buffer too small; required=%zu", instance.pending.size() + 1);
            return EMS_PLUGIN_BUFFER_TOO_SMALL;
        }
        std::memcpy(output, instance.pending.c_str(), instance.pending.size() + 1);
        instance.pending.clear();
        return instance.pending_count;
    });
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_write(void* opaque, const char* code,
                                                       double value) {
    return invoke(opaque, [&](Instance& instance) {
        if (!code) {
            instance.set_error("Point code is null");
            return -1;
        }
        instance.pending.clear();
        return instance.protocol.write(code, value);
    });
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_is_connected(void* opaque) {
    return invoke(opaque,
                  [](Instance& instance) { return instance.protocol.is_connected() ? 1 : 0; });
}

EMS_PLUGIN_EXPORT const char* EMS_PLUGIN_CALL ems_plugin_last_error(void* opaque) {
    if (!opaque) return "No instance";
    auto* instance = static_cast<Instance*>(opaque);
    try {
        std::lock_guard<std::mutex> lock(instance->mutex);
        if (!instance->error[0]) instance->set_error(instance->protocol.last_error().c_str());
        return instance->error.data();
    } catch (...) {
        return "Cannot obtain plugin error";
    }
}

}  // extern "C"
