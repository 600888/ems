#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief MMS PDU 观察回调，connection 为当前会话标识。 */
typedef void (*ems_mms_traffic_fn)(void* user, int sent, const uint8_t* data, int size,
                                   uintptr_t connection);

/**
 * @brief 在监听前为 MMS 服务端登记观察器。
 * @param[in,out] server MMS 服务端对象。
 * @param[in] callback 收发回调。
 * @param[in,out] user 借用上下文，须覆盖所有会话生命周期。
 */
void ems_mms_set_traffic(void* server, ems_mms_traffic_fn callback, void* user);

/**
 * @brief 为 MMS 服务端的 ISO 监听器登记观察器，仅集成栈内部使用。
 * @param[in,out] server ISO 服务端。
 * @param[in] callback 观察回调。
 * @param[in,out] user 借用上下文。
 */
void ems_iso_set_traffic(void* server, ems_mms_traffic_fn callback, void* user);

/**
 * @brief 在 ISO 会话边界观察 MMS PDU，仅集成栈内部使用。
 * @param[in] server ISO 服务端。
 * @param[in] sent 非零发送，零接收。
 * @param[in] data 借用 PDU。
 * @param[in] size PDU 字节数。
 * @param[in] connection 会话标识。
 */
void ems_iso_traffic(void* server, int sent, const uint8_t* data, int size, uintptr_t connection);

#ifdef __cplusplus
}
#endif
