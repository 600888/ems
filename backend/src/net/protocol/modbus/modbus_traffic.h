#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief libmodbus 构建副本的字节观察回调，与公开插件 ABI 无关。 */
typedef void (*ems_modbus_traffic_fn)(void* user, int sent, const uint8_t* data, int size,
                                      uintptr_t connection);

/**
 * @brief 在开始 I/O 前安装集成栈的实例观察器。
 * @param[in,out] context libmodbus 上下文。
 * @param[in] callback 回调，空关闭观察。
 * @param[in,out] user 借用上下文，存活至停止通信。
 */
void ems_modbus_set_traffic(void* context, ems_modbus_traffic_fn callback, void* user);

#ifdef __cplusplus
}
#endif
