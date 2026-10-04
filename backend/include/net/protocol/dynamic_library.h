/** @file dynamic_library.h
 * @brief 跨平台动态库 RAII 封装；原生句柄与系统头隐藏在 PIMPL 中。
 */
#ifndef EMS_NET_PROTOCOL_DYNAMIC_LIBRARY_H
#define EMS_NET_PROTOCOL_DYNAMIC_LIBRARY_H

#include <memory>
#include <string>

namespace ems::net::protocol {

class DynamicLibrary final {
   public:
    /** @brief 创建未加载的动态库。 */
    DynamicLibrary();
    /** @brief 释放本对象持有的系统库引用；调用方须已停止全部插件代码。 */
    ~DynamicLibrary();
    /** @brief 禁止复制库所有者。 @param[in] other 源对象。 */
    DynamicLibrary(const DynamicLibrary& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 源对象。
     * @return 不可调用。
     */
    DynamicLibrary& operator=(const DynamicLibrary& other) = delete;
    /** @brief 转移系统库引用。 @param[in,out] other 转移后为空的源对象。 */
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    /**
     * @brief 释放当前引用并转移源对象引用。
     * @param[in,out] other 转移后为空的源对象。
     * @return 当前对象。
     */
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    /**
     * @brief 加载 UTF-8 路径指定的库；失败保留诊断信息。
     * @param[in] path 库路径，内部转换为绝对路径。
     * @return true 成功；已加载或路径无效时返回 false。
     */
    bool open(const std::string& path);
    /**
     * @brief 查找导出符号；返回地址的有效期不超过库引用。
     * @param[in] name 精确导出名称。
     * @return 符号地址；未加载或查找失败返回 nullptr。
     */
    void* symbol(const char* name);
    /**
     * @brief 关闭库引用；调用前必须保证没有在途调用和实例。
     * @return true 成功或原本未加载；失败保留句柄与诊断。
     */
    bool close() noexcept;
    /** @brief 查询是否持有库句柄。 @return true 表示已加载。 */
    bool is_open() const noexcept;
    /** @brief 返回错误文本副本。 @return 最近一次平台诊断。 */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::net::protocol
#endif
