/**
 * @file protocol_plugin_api.h
 * @brief 协议插件标准 C 接口定义
 *
 * 每个协议动态库 (.dll/.so) 必须导出以下 C 函数符号，
 * 采集引擎通过跨平台加载器在运行时按需加载。
 *
 * ABI 稳定性说明:
 *   - 采用纯 C 接口避免 C++ 名称修饰 (mangling) 和 ABI 兼容性问题
 *   - 仍须匹配平台、架构、结构布局、调用约定与运行依赖，并实测兼容性
 *   - 插件内部使用 C++ 实现，仅边界使用 C 函数桥接
 *
 * @note 本文件由采集引擎和所有协议插件共同引用。
 */
#ifndef EMS_NET_PROTOCOL_PLUGIN_API_H
#define EMS_NET_PROTOCOL_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define EMS_PLUGIN_CALL __cdecl
#if defined(EMS_BUILD_PROTOCOL_PLUGIN)
#define EMS_PLUGIN_EXPORT __declspec(dllexport)
#else
#define EMS_PLUGIN_EXPORT
#endif
#else
#define EMS_PLUGIN_CALL
#define EMS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 版本常量
 * ============================================================ */

#define EMS_PLUGIN_API_VERSION 1
#define EMS_PLUGIN_BUFFER_TOO_SMALL (-2)
#define EMS_PLUGIN_RESULT_BUF_SIZE (64 * 1024)  // 64KB 结果缓冲区

/** @brief 相对于插件实例的收发方向；失败发送单独标记，不能当作成功发送。 */
enum EmPluginTrafficDirection { EMS_TRAFFIC_RX = 0, EMS_TRAFFIC_TX = 1, EMS_TRAFFIC_TX_FAILED = 2 };

/**
 * @brief 观察协议栈收发的字节；缓冲区仅在回调期间借用，回调不得抛异常或重入插件。
 * @param[in,out] user 宿主注册的上下文。
 * @param[in] direction EmPluginTrafficDirection，TX 表示栈发送观察点，不表示远端确认。
 * @param[in] data 原始字节，可能是分片、粘帧或 MMS PDU，不保证一条事件对应一帧。
 * @param[in] size 原始字节数。
 * @param[in] timestamp_us 观察时的 UTC 微秒时标。
 * @param[in] connection_id 服务端连接标识；0 表示未区分连接。
 */
typedef void(EMS_PLUGIN_CALL* ems_plugin_traffic_callback)(void* user, int direction,
                                                           const uint8_t* data, size_t size,
                                                           int64_t timestamp_us,
                                                           uint64_t connection_id);

/**
 * @brief 可选收发观察扩展，不改变必需 ABI；注销返回前等待在途回调结束。
 * @param[in,out] instance 插件实例，调用方须串行管理其生命周期。
 * @param[in] callback 回调，传空注销；回调中禁止调用本函数。
 * @param[in,out] user 借用的宿主上下文，注销返回后可释放。
 * @return 0 成功；-1 参数错误。
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_set_traffic_handler_fn)(
    void* instance, ems_plugin_traffic_callback callback, void* user);

/* ============================================================
 * 插件信息结构体
 * ============================================================ */

/**
 * @brief 插件元信息
 *
 * 通过 ems_plugin_info() 获取，供 PluginManager 识别协议类型。
 */
typedef struct EmPluginInfo {
    int api_version;  // 必须等于 EMS_PLUGIN_API_VERSION
    const char*
        protocol_name;  // 协议名称: "ModbusTCP", "ModbusRTU", "IEC104", "DLT645", "DLT698", ...
    const char* description;  // 简要描述
    const char* version;      // 插件版本号, 如 "1.0.0"
} EmPluginInfo;

/* ============================================================
 * 插件必须导出的 C 函数符号
 *
 * 除状态查询与批量读取外，操作接口返回 0 表示成功，非 0 表示失败。
 * 错误详情可通过 ems_plugin_last_error() 获取。
 * ============================================================ */

/**
 * @brief 获取插件信息
 * @return 指向静态 EmPluginInfo 的指针（生命周期：持有对应动态库引用期间有效）
 */
typedef const EmPluginInfo*(EMS_PLUGIN_CALL* ems_plugin_info_fn)(void);

/**
 * @brief 创建插件实例
 * @return 不透明指针 (void*)，后续所有操作均通过此指针进行
 */
typedef void*(EMS_PLUGIN_CALL* ems_plugin_create_fn)(void);

/**
 * @brief 销毁插件实例
 * @param[in] instance 由 ems_plugin_create() 返回的指针
 */
typedef void(EMS_PLUGIN_CALL* ems_plugin_destroy_fn)(void* instance);

/**
 * @brief 初始化插件（传入 JSON 配置）
 * @param[in] instance    插件实例指针
 * @param[in] json_config JSON 格式的配置字符串
 * @return 0 成功, -1 失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_init_fn)(void* instance, const char* json_config);

/**
 * @brief 连接设备
 * @param[in] instance 插件实例指针
 * @return 0 成功, -1 失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_connect_fn)(void* instance);

/**
 * @brief 断开连接
 * @param[in] instance 插件实例指针
 * @return 0 成功, -1 失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_disconnect_fn)(void* instance);

/**
 * @brief 批量读取测点值
 *
 * 读取一批测点的当前值，结果以 JSON 数组格式写入 result_json。
 *
 * 输入 point_codes 格式示例:
 *   ["1:3:0:FLOAT_BE", "1:3:2:UINT16", "2:3:0:FLOAT_BE"]
 *
 * 输出 result_json 格式示例:
 *   [
 *     {"code":"1:3:0:FLOAT_BE","value":123.45,"quality":0,"timestamp":1700000000000},
 *     {"code":"1:3:2:UINT16","value":100,"quality":0,"timestamp":1700000000000}
 *   ]
 *
 * @param[in] instance      插件实例指针
 * @param[in] point_codes   测点编码数组
 * @param[in] point_count   测点数量
 * @param[out] result_json   输出缓冲区，接收 JSON 结果
 * @param[in] result_buf_size 输出缓冲区大小
 * @return >=0 成功点数；-2 缓冲不足（同编码重试复用结果）；-1 其他错误
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_read_batch_fn)(void* instance,
                                                       const char* const point_codes[],
                                                       int point_count, char* result_json,
                                                       int result_buf_size);

/**
 * @brief 写入单个测点值
 * @param[in] instance  插件实例指针
 * @param[in] point_code 测点编码
 * @param[in] value     写入值
 * @return 0 成功, -1 失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_write_fn)(void* instance, const char* point_code,
                                                  double value);

/**
 * @brief 可选的带命令期限写入扩展，期限覆盖插件内各阶段，旧插件可不导出。
 * @param[in,out] instance 借用的插件实例。
 * @param[in] point_code 已配置的写入点编码。
 * @param[in] value 原始写入值。
 * @param[in] expires_at_ms UTC 毫秒绝对期限，过期不得发送。
 * @return 0 协议确认；-1 失败，诊断由 last_error 提供。
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_write_until_fn)(void* instance, const char* point_code,
                                                        double value, int64_t expires_at_ms);

/**
 * @brief 可选的服务端映像批量更新扩展；旧插件可不导出，不改变必需 ABI。
 * @param[in] instance 服务端实例。
 * @param[in] samples_json 数组，每项含 code、value（null 保留旧值）、iec_quality、timestamp。
 * @return 0 全批校验后在模型锁内提交；-1 不支持或校验失败，映像不变。
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_update_image_fn)(void* instance, const char* samples_json);

/**
 * @brief 可选的服务端北向控制队列扩展；旧插件可不导出。
 *
 * 服务端受理上级控制后按到达顺序排队，由宿主取出并交给本地命令管线。
 * 每个请求只交付一次；宿主未回执前保持待定，超时由插件自行收敛。
 *
 * @param[in,out] instance 服务端实例。
 * @param[out] result_json 请求数组，元素含 id、point_id、code、value、expires_at_ms。
 * @param[in] result_buf_size 输出缓冲区字节数。
 * @return >=0 本次交付的请求数；-2 缓冲不足（同一次调用扩容重取）；-1 不支持或失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_take_controls_fn)(void* instance, char* result_json,
                                                          int result_buf_size);

/**
 * @brief 可选的服务端控制回执扩展；旧插件可不导出。
 *
 * 宿主完成本地下发后回执执行结果，插件据此向上级发送正/负结束确认。
 *
 * @param[in,out] instance 服务端实例。
 * @param[in] control_id take_controls 返回的请求 ID。
 * @param[in] accepted 非 0 表示执行成功。
 * @param[in] reason 失败原因，可空；最长保留 256 字节。
 * @return 0 已发送或会话已关闭；-1 未知 ID 或发送失败
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_resolve_control_fn)(void* instance, int64_t control_id,
                                                            int accepted, const char* reason);

/**
 * @brief 获取连接状态
 * @param[in] instance 插件实例指针
 * @return 1 已连接, 0 未连接, -1 错误
 */
typedef int(EMS_PLUGIN_CALL* ems_plugin_is_connected_fn)(void* instance);

/**
 * @brief 获取最近一次错误信息
 * @param[in] instance 插件实例指针
 * @return 错误描述字符串（生命周期：下次调用前有效）
 */
typedef const char*(EMS_PLUGIN_CALL* ems_plugin_last_error_fn)(void* instance);

/* ============================================================
 * 插件导出符号名定义（供 dlsym 使用）
 * ============================================================ */

#define EMS_PLUGIN_INFO_SYMBOL "ems_plugin_info"
#define EMS_PLUGIN_CREATE_SYMBOL "ems_plugin_create"
#define EMS_PLUGIN_DESTROY_SYMBOL "ems_plugin_destroy"
#define EMS_PLUGIN_INIT_SYMBOL "ems_plugin_init"
#define EMS_PLUGIN_CONNECT_SYMBOL "ems_plugin_connect"
#define EMS_PLUGIN_DISCONNECT_SYMBOL "ems_plugin_disconnect"
#define EMS_PLUGIN_READ_BATCH_SYMBOL "ems_plugin_read_batch"
#define EMS_PLUGIN_WRITE_SYMBOL "ems_plugin_write"
#define EMS_PLUGIN_WRITE_UNTIL_SYMBOL "ems_plugin_write_until"
#define EMS_PLUGIN_UPDATE_IMAGE_SYMBOL "ems_plugin_update_image"
#define EMS_PLUGIN_TAKE_CONTROLS_SYMBOL "ems_plugin_take_controls"
#define EMS_PLUGIN_RESOLVE_CONTROL_SYMBOL "ems_plugin_resolve_control"
#define EMS_PLUGIN_IS_CONNECTED_SYMBOL "ems_plugin_is_connected"
#define EMS_PLUGIN_LAST_ERROR_SYMBOL "ems_plugin_last_error"
#define EMS_PLUGIN_SET_TRAFFIC_HANDLER_SYMBOL "ems_plugin_set_traffic_handler"

#ifdef __cplusplus
}
#endif

#endif  // EMS_NET_PROTOCOL_PLUGIN_API_H
