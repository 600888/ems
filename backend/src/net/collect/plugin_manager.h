/**
 * @file plugin_manager.h
 * @brief 协议插件管理器
 *
 * 管理动态库 (.so) 的加载/卸载，维护 协议名 -> 插件工厂 的映射，
 * 提供按需加载 (dlopen/dlsym) 和引用计数的生命周期管理。
 *
 * 核心原则: 用到时才加载，不用不加载。
 * 同一协议名（如 ModbusTCP/ModbusRTU 都映射到 libproto_modbus.so）共享同一个 dlopen 句柄。
 */
#ifndef EMS_NET_COLLECT_PLUGIN_MANAGER_H
#define EMS_NET_COLLECT_PLUGIN_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/protocol/common/protocol_plugin_api.h"

namespace ems {
namespace net {
namespace collect {

/**
 * @brief 插件管理器
 *
 * 线程安全：所有公开方法均持有内部互斥锁。
 */
class PluginManager {
   public:
    /**
     * @brief 协议 -> 动态库文件名 映射条目
     */
    struct LibMapping {
        std::string protocolName;
        std::string libFileName;
    };

    /**
     * @brief 所有函数指针的便捷结构
     */
    struct PluginFunctions {
        ems_plugin_info_fn info_fn{nullptr};
        ems_plugin_create_fn create_fn{nullptr};
        ems_plugin_destroy_fn destroy_fn{nullptr};
        ems_plugin_init_fn init_fn{nullptr};
        ems_plugin_connect_fn connect_fn{nullptr};
        ems_plugin_disconnect_fn disconnect_fn{nullptr};
        ems_plugin_read_batch_fn read_batch_fn{nullptr};
        ems_plugin_write_fn write_fn{nullptr};
        ems_plugin_is_connected_fn is_connected_fn{nullptr};
        ems_plugin_last_error_fn last_error_fn{nullptr};

        explicit operator bool() const { return create_fn != nullptr && destroy_fn != nullptr; }
    };

    /** @brief 构造 PluginManager 实例。 */
    PluginManager();
    /** @brief 释放 PluginManager 持有的资源。 */
    ~PluginManager();

    // ---- 拷贝/赋值 禁用 ----
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    PluginManager(const PluginManager& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     */
    PluginManager& operator=(const PluginManager& other) = delete;
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    PluginManager(PluginManager&& other) noexcept = delete;
    /**
     * @brief 禁止复制或移动赋值。
     * @param[in] other 待赋值的源对象。
     */
    PluginManager& operator=(PluginManager&& other) noexcept = delete;

    /**
     * @brief 设置动态库搜索目录
     * @param[in] dir 绝对或相对路径，默认 "./lib"
     */
    void set_plugin_dir(const std::string& dir);

    /**
     * @brief 获取动态库搜索目录
     * @return 对应对象的引用。
     */
    const std::string& plugin_dir() const { return plugin_dir_; }

    /**
     * @brief 加载指定协议的插件
     *
     * 如果该协议已加载则增加引用计数并复用已有句柄。
     *
     * @param[in] protocol_name 协议名称, 如 "ModbusTCP", "IEC104"
     * @return 插件实例指针, 失败返回 nullptr
     */
    void* load(const std::string& protocol_name);

    /**
     * @brief 卸载指定协议的插件（引用计数减一）
     *
     * 引用计数为 0 时真正执行 dlclose 卸载。
     *
     * @param[in] protocol_name 协议名称
     */
    void unload(const std::string& protocol_name);

    /**
     * @brief 检查协议是否已加载
     * @param[in] protocol_name 协议名称。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_loaded(const std::string& protocol_name) const;

    /**
     * @brief 获取已加载的所有协议名
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> loaded_protocols() const;

    /**
     * @brief 获取插件信息
     * @param[in] instance 插件实例指针
     * @return 插件信息指针, 失败返回 nullptr
     */
    const EmPluginInfo* get_plugin_info(void* instance) const;

    /**
     * @brief 获取已加载插件的函数指针
     * @param[in] protocol_name 协议名称
     * @return PluginFunctions（如果协议未加载则返回空的 PluginFunctions）
     */
    PluginFunctions get_functions(const std::string& protocol_name) const;

    /**
     * @brief 获取最近一次错误信息
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const { return last_err_; }

    /**
     * @brief 获取默认的协议-库文件名映射表
     * @return 对应对象的引用。
     */
    static const std::unordered_map<std::string, std::string>& default_lib_map();

    /**
     * @brief 注册自定义协议映射（覆盖默认映射）
     * @param[in] protocol_name 协议名称。
     * @param[in] lib_name 动态库文件名。
     */
    void register_mapping(const std::string& protocol_name, const std::string& lib_name);

   private:
    /**
     * @brief 已加载的插件条目
     */
    struct PluginEntry {
        void* dl_handle{nullptr};
        PluginFunctions fns;
        int ref_count{0};
    };

    /**
     * @brief 解析协议插件的动态库路径。
     * @param[in] protocol_name 协议名称。
     * @return 对应的文本内容。
     */
    std::string resolve_lib_path(const std::string& protocol_name) const;
    /**
     * @brief 从动态库解析插件函数入口。
     * @param[in] handle 动态库句柄。
     * @param[in,out] fns 插件函数表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool load_symbols(void* handle, PluginFunctions& fns);

    mutable std::mutex mutex_;
    std::string plugin_dir_{"./lib"};
    std::unordered_map<std::string, std::string> custom_mappings_;
    std::unordered_map<std::string, PluginEntry> loaded_plugins_;
    mutable std::string last_err_;
};

}  // namespace collect
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_COLLECT_PLUGIN_MANAGER_H
