/**
 * @file plugin_manager.h
 * @brief 协议插件管理器
 *
 * 通过跨平台 PIMPL 加载器管理动态库与插件实例，
 * 提供按需加载与按实例持有库租约的生命周期管理。
 *
 * 核心原则: 用到时才加载，不用不加载。
 * ModbusTCP/ModbusRTU 按规范化库路径共享句柄，运行状态按实例隔离。
 */
#ifndef EMS_NET_PROTOCOL_PLUGIN_MANAGER_H
#define EMS_NET_PROTOCOL_PLUGIN_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/protocol/protocol_plugin_api.h"

namespace ems {
namespace net {
namespace collect {

/**
 * @brief 插件管理器
 *
 * 目录/缓存/实例登记线程安全；销毁前调用方必须等待实例所有在途调用退出。
 * 插件构造和销毁在管理器全局锁之外执行。
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

        /** @brief 判断必需工厂入口是否可用。 @return true 表示入口存在。 */
        explicit operator bool() const { return create_fn != nullptr && destroy_fn != nullptr; }
    };

    /** @brief 构造 PluginManager 实例。 */
    PluginManager();
    /** @brief 销毁所有剩余实例；调用前必须停止并 join 全部通道工作线程。 */
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
     * @return UTF-8 目录副本。
     */
    std::string plugin_dir() const;

    /**
     * @brief 加载指定协议的插件
     *
     * 相同库路径复用句柄，每次创建独立实例并持有租约；失败不保留引用。
     *
     * @param[in] protocol_name 协议名称, 如 "ModbusTCP", "IEC104"
     * @return 管理器拥有的实例指针，使用完调用 release；失败返回 nullptr
     */
    void* load(const std::string& protocol_name);

    /**
     * @brief 停止并在插件内销毁实例，随后释放该实例的库租约。
     * @param[in] instance load 返回的实例；调用前须已停止使用其函数表。
     * @return true 已销毁；未知实例、停止失败或销毁失败时返回 false。
     */
    bool release(void* instance);

    /** @brief 统计仍有租约的库。 @return 不同规范化库路径数量。 */
    std::size_t loaded_library_count() const;

    /**
     * @brief 销毁指定协议的一份实例并释放租约（兼容入口，优先使用 release）
     *
     * 调用前必须已停止对应实例的全部操作；最后实例销毁后释放系统库引用。
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
     * @return 插件信息借用指针，实例释放前有效；未知实例返回 nullptr
     */
    const EmPluginInfo* get_plugin_info(void* instance) const;

    /**
     * @brief 获取已加载插件的函数指针
     * @param[in] protocol_name 协议名称
     * @return 函数表；有效期不超过对应实例租约；未加载或名称对应多个包时返回空表
     */
    PluginFunctions get_functions(const std::string& protocol_name) const;

    /**
     * @brief 按实例取得匹配的函数表，避免配置换包后名称查找产生歧义。
     * @param[in] instance load 返回的存活实例。
     * @return 与该实例属于同一库的函数表；未知实例返回空表。
     */
    PluginFunctions get_instance_functions(void* instance) const;

    /**
     * @brief 获取最近一次错误信息
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const;

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
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace collect
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_PROTOCOL_PLUGIN_MANAGER_H
