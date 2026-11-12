/** @file device.hpp
 * @brief 与连接生命周期独立的线程安全设备数据入口。
 */
#pragma once
#include <dlt698/service/object.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::app {
class Server;
}

namespace dlt698::service {
struct DeviceOptions {
    standard::DeviceLayout layout;
    Limits limits;
    std::size_t max_objects = 256;                   ///< 含自定义声明的对象数上限。
    std::size_t max_attributes = 4096;               ///< 含自定义声明的属性数上限。
    std::size_t max_value_bytes = 16 * 1024 * 1024;  ///< 所有已发布值的编码字节总量上限。
};

/** @brief 本地数据设备，默认只读地对外发布标准属性，可被多个服务器共享。
 * @note 数据保存在内存中；连接断开或服务器停止不清空数据。单次更新原子发布，
 * 不承诺不同属性之间的事务一致性，不推断真实计量能力。
 */
class Device {
   public:
    /** @brief 创建空设备并验证布局及非零资源预算。
     * @param[in] options 设备布局、单值和设备总量上限。
     * @throws std::invalid_argument 布局非法或预算为零。
     */
    DLT698_API explicit Device(DeviceOptions options = {});
    /** @brief 释放设备持有的数据，正在读取的快照仍由读取方持有。 */
    DLT698_API ~Device();
    /** @brief 禁止复制设备身份。 */
    Device(const Device&) = delete;
    /** @brief 禁止复制赋值设备。 */
    Device& operator=(const Device&) = delete;
    /** @brief 设置标准属性或已声明的自定义属性，首次设置后即可对外读取。
     * @param[in] attribute 特征零、索引零的完整 OAD。
     * @param[in] value 拥有内存的精确协议值，不隐式换算单位或补全数组。
     * @return 成功或定义、类型、布局、资源错误；失败保留旧值和目录。
     * @note 可在运行中由业务线程调用；本地更新不授予远端 SET 权限。
     */
    DLT698_API Result<void> set(model::Oad attribute, model::Data value);
    /** @brief 更新已发布数组或结构的一个一级元素。
     * @param[in] attribute 特征零、非零索引的 OAD；索引从 1 开始。
     * @param[in] value 元素的精确类型值。
     * @return 成功或属性不存在、索引、类型、布局及资源错误；失败不改旧值。
     */
    DLT698_API Result<void> set_element(model::Oad attribute, model::Data value);
    /** @brief 本地读取完整属性或一级元素的拥有内存的快照。
     * @param[in] attribute 完整 OAD；索引零读取整体。
     * @return Data 或原始 DAR：3 拒绝、4 未发布、8 非法索引。
     */
    DLT698_API ObjectValue get(model::Oad attribute) const;
    /** @brief 声明厂家自定义只读数据对象，不发布未设置的属性。
     * @param[in] schema 未收录的 OI，至少一个普通只读属性；不支持方法或记录声明。
     * @return 成功或重复定义、标准 OI 冲突、非法 schema 及资源错误。
     * @note 类型声明后不能替换；远端写入和动态 provider 使用现有高级分层 API。
     */
    DLT698_API Result<void> define(ObjectSchema schema);

   private:
    /** @brief 向高层服务器提供受控目录，不允许调用方绕过数据发布约定。
     * @return 保持目录和已发布快照存活的共享引用。
     */
    DLT698_API std::shared_ptr<ObjectRegistry> objects() const;
    friend class dlt698::app::Server;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace dlt698::service
