#include <cstdio>
#include <cstring>
#include <new>

#include "net/protocol/protocol_plugin_api.h"

extern "C" {
EMS_PLUGIN_EXPORT const EmPluginInfo* EMS_PLUGIN_CALL ems_plugin_info(void) {
#ifdef EMS_TEST_BAD_ABI
    constexpr int version = 99;
#else
    constexpr int version = EMS_PLUGIN_API_VERSION;
#endif
    static const EmPluginInfo info = {version, "fixture", "lifecycle fixture", "1"};
    return &info;
}

EMS_PLUGIN_EXPORT void* EMS_PLUGIN_CALL ems_plugin_create(void) {
#ifdef EMS_TEST_NULL_CREATE
    return nullptr;
#else
    return new (std::nothrow) int(7);
#endif
}

#ifndef EMS_TEST_MISSING_SYMBOL
EMS_PLUGIN_EXPORT void EMS_PLUGIN_CALL ems_plugin_destroy(void* instance) {
    delete static_cast<int*>(instance);
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_init(void* instance, const char* config) {
    *static_cast<int*>(instance) = config && std::strcmp(config, "fail_stop") == 0 ? 9 : 7;
    return 0;
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_connect(void*) { return 0; }

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_disconnect(void* instance) {
    return *static_cast<int*>(instance) == 9 ? -1 : 0;
}

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_is_connected(void*) { return 1; }

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_write(void*, const char*, double) { return 0; }

EMS_PLUGIN_EXPORT const char* EMS_PLUGIN_CALL ems_plugin_last_error(void*) { return ""; }

EMS_PLUGIN_EXPORT int EMS_PLUGIN_CALL ems_plugin_read_batch(void*, const char*[], int, char* output,
                                                            int size) {
    if (!output || size < 3) return -1;
    std::snprintf(output, size, "[]");
    return 0;
}
#endif
}
