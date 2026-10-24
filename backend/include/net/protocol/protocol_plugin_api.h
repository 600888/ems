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
#define EMS_PLUGIN_IS_CONNECTED_SYMBOL "ems_plugin_is_connected"
#define EMS_PLUGIN_LAST_ERROR_SYMBOL "ems_plugin_last_error"

#ifdef __cplusplus
}
#endif

#endif  // EMS_NET_PROTOCOL_PLUGIN_API_H
