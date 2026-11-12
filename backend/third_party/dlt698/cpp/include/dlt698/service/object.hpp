/** @file object.hpp
 * @brief 通用对象 schema、读写/方法 provider 和内存模拟对象。
 */
#pragma once
#include <dlt698/export.hpp>
#include <dlt698/protocol/apdu/mutation.hpp>
#include <functional>
#include <memory>

namespace dlt698::service {
class Device;
using ObjectValue = std::variant<std::uint8_t, model::Data>;  ///< DAR 或精确类型 Data。

struct ActionValue {
    std::uint8_t dar = 0;
    std::optional<model::Data> data;
};

struct AttributeSchema {
    std::uint8_t number = 2;
    model::DataType type = model::DataType::null;
    bool readable = true;
    bool writable = false;  ///< 写入须显式授权，已有读取 schema 不会自动变成可写。
    bool record = false;    ///< 记录型属性须显式声明，由 read_record 解释行列选择。
};

struct MethodSchema {
    std::uint8_t number = 1;
    std::optional<model::DataType> parameter_type;
    std::optional<model::DataType> return_type;
    bool executable = true;
};

struct ObjectSchema {
    std::uint16_t oi = 0;
    std::string name;
    std::vector<AttributeSchema> attributes;
    std::vector<MethodSchema> methods{};
};

// 必须整类导出：跨动态库派生类需要基类的 typeinfo 和 vtable，
// 只导出成员函数时这两个符号在 ELF/Mach-O 上仍是隐藏的，链接会失败。
class DLT698_API IObjectProvider {
   public:
    /** @brief 通过接口释放 provider。 */
    virtual ~IObjectProvider() = default;
    /** @brief 读取一个属性，快照特征及元素索引由 provider 解释。
     * @param[in] attribute 完整 OAD，不隐式丢弃属性特征位。
     * @return 精确 Data 或原始 DAR；不得阻塞等待调用它的会话执行器。
     */
    virtual ObjectValue read(const model::Oad& attribute) = 0;
    /** @brief 查询记录，默认返回拒绝 DAR=3。
     * @param[in] query 完整 OAD/RSD/RCSD；空 RCSD 为全选，实际响应必须给出表头。
     * @return 拥有所有列与行内存的快照或 DAR；提供者负责选择器业务语义。
     * @note 调用期间不得保留借用引用或阻塞同一执行器，跨查询的一致性由提供者决定。
     */
    virtual protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query);
    /** @brief 写入属性，默认拒绝以保持既有只读 provider 合约。
     * @param[in] attribute 完整 OAD，索引/特征由 provider 解释。
     * @param[in] value 精确 Data，调用期间有效，不得保留借用引用。
     * @return 原始 DAR，0 成功；非零不得自动重试。
     */
    virtual std::uint8_t write(const model::Oad& attribute, const model::Data& value);
    /** @brief 执行方法，默认拒绝；调用发生在会话执行器中。
     * @param[in] method 完整 OMD，模式由 provider 解释。
     * @param[in] parameter 参数 Data，不得保留借用引用。
     * @return DAR 与可选拥有内存的数据；不得阻塞等待同一执行器。
     */
    virtual ActionValue invoke(const model::Omd& method, const model::Data& parameter);
};

class ObjectRegistry {
   public:
    /** @brief 创建空对象目录。 */
    DLT698_API ObjectRegistry();
    /** @brief 释放目录中的 schema 和 provider。 */
    DLT698_API ~ObjectRegistry();
    /** @brief 禁止复制目录及 provider 所有权。 */
    ObjectRegistry(const ObjectRegistry&) = delete;
    /** @brief 禁止复制赋值目录。 */
    ObjectRegistry& operator=(const ObjectRegistry&) = delete;
    /** @brief 注册对象及 provider。
     * @param[in] schema 至少定义一个属性或方法；属性编号 1 至 31、方法编号 1 至 255，各自不得重复。
     * @param[in] provider 非空读取 provider，须保证其并发读取合约。
     * @return 成功或 schema 非法、OI 重复的错误。
     */
    DLT698_API Result<void> register_object(ObjectSchema schema,
                                            std::shared_ptr<IObjectProvider> provider);
    /** @brief 按 schema 权限及类型检查读取属性。
     * @param[in] attribute 原始 OAD。
     * @return Data 或 DAR（3=拒绝、4=未定义、7=类型不匹配、255=provider 异常）。
     * @note 释放目录锁后调用 provider，索引非零的元素类型由 provider 保证。
     */
    DLT698_API ObjectValue read(const model::Oad& attribute) const;
    /** @brief 检查记录声明与读权限后查询，目录锁外调用 provider。
     * @param[in] query 完整记录查询。
     * @return 拥有行列内存的结果；3 拒绝、4 未定义、5 非记录属性、255 提供者异常。
     */
    DLT698_API protocol::apdu::RecordResult read_record(
        const protocol::apdu::GetRecord& query) const;
    /** @brief 检查写权限和完整属性类型后调用 provider。
     * @param[in] attribute 精确 OAD，索引非零的元素校验由 provider 负责。
     * @param[in] value 写入的精确 Data。
     * @return DAR：0 成功、3 拒绝、4 未定义、7 类型不匹配、255 异常。
     */
    DLT698_API std::uint8_t write(const model::Oad& attribute, const model::Data& value) const;
    /** @brief 检查方法权限与参数/返回类型后调用 provider。
     * @param[in] method 完整 OMD。
     * @param[in] parameter 方法参数。
     * @return 原始 DAR 和可选 Data，provider 异常转为 DAR=255。
     */
    DLT698_API ActionValue invoke(const model::Omd& method, const model::Data& parameter) const;

   private:
    /** @brief 原子发布设备已校验的 schema 和不可变数据提供者。
     * @param[in] schema 由 Device 准备的完整对象定义，不改变已有属性类型和权限。
     * @param[in] provider 拥有完整数据快照的非空提供者。
     * @note 仅供 Device 使用；旧读取持有旧提供者，不观察半完成的属性扩充。
     */
    void publish_object(ObjectSchema schema, std::shared_ptr<IObjectProvider> provider);
    friend class Device;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief 线程安全的内存模拟对象，仅支持特征零和数组/结构的一级元素索引。 */
class MemoryObject final : public IObjectProvider {
   public:
    /** @brief 创建没有属性值的模拟对象。 */
    DLT698_API MemoryObject();
    /** @brief 释放模拟属性值。 */
    DLT698_API ~MemoryObject() override;
    /** @brief 禁止复制模拟对象。 */
    MemoryObject(const MemoryObject&) = delete;
    /** @brief 禁止复制赋值模拟对象。 */
    MemoryObject& operator=(const MemoryObject&) = delete;
    /** @brief 设置模拟属性的完整值，此方法是本地配置，不是协议 SET 服务。
     * @param[in] attribute 1 至 31 的属性编号。
     * @param[in] value 精确类型属性值。
     * @throws std::invalid_argument 属性编号非法。
     */
    DLT698_API void set(std::uint8_t attribute, model::Data value);
    /** @brief 按属性和索引读取值。
     * @param[in] attribute 完整 OAD；索引零为全部内容，非零从 1 开始。
     * @return Data 或 DAR，不存在的属性为 4，非法索引为 8。
     */
    DLT698_API ObjectValue read(const model::Oad& attribute) override;
    /** @brief 原子替换已有属性或一级元素，不创建未知属性。
     * @param[in] attribute 特征零的 OAD，索引零替换整体，非零从 1 开始。
     * @param[in] value 须与被替换值的类型一致。
     * @return 原始 DAR，非法索引为 8，类型不匹配为 7。
     */
    DLT698_API std::uint8_t write(const model::Oad& attribute, const model::Data& value) override;
    /** @brief 注册模拟方法，可在回调中重入读写本对象。
     * @param[in] number 非零方法编号。
     * @param[in] handler 非空回调，接收完整 OMD 和参数；须满足并发调用约定。
     * @throws std::invalid_argument 编号为零或回调为空。
     */
    DLT698_API void bind_method(
        std::uint8_t number,
        std::function<ActionValue(const model::Omd&, const model::Data&)> handler);
    /** @brief 注册应用提供的模拟记录查询器，在对象锁外调用。
     * @param[in] attribute 1 至 31 的记录属性编号。
     * @param[in] handler 非空处理器，须返回拥有内存且列宽一致的快照，解释全部所需选择器。
     * @throws std::invalid_argument 属性编号非法或回调为空。
     */
    DLT698_API void bind_record(
        std::uint8_t attribute,
        std::function<protocol::apdu::RecordResult(const protocol::apdu::GetRecord&)> handler);
    /** @brief 查询已绑定的记录，未绑定返回 DAR=4。
     * @param[in] query 完整查询；处理器可重入本对象的读写操作。
     * @return 记录快照或 DAR；异常由目录转为 255。
     */
    DLT698_API protocol::apdu::RecordResult read_record(
        const protocol::apdu::GetRecord& query) override;
    /** @brief 调用已注册模拟方法，仅接受模式零。
     * @param[in] method 精确 OMD。
     * @param[in] parameter 精确参数。
     * @return 方法结果；未定义返回 DAR=4，非零模式返回 DAR=3，异常由目录隔离。
     */
    DLT698_API ActionValue invoke(const model::Omd& method, const model::Data& parameter) override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace dlt698::service
