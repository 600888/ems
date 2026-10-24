#pragma once

#include <realtime/realtime_store.h>

#include <memory>

namespace ems::realtime {

/**
 * @brief 配置驱动的动态库公共入口，封装第三方后端及本进程资源。
 *
 * 首批提供记录级 API，不替代尚未接入的类型化来源授权门面。
 * 调用者与动态库须使用兼容的编译器 ABI、架构和 CRT。
 */
class EMS_RTDB_API RealtimeClient final {
   public:
    /**
     * @brief 按部署 JSON 打开统一客户端；共享内存仅允许挂接已有段。
     * @param[in] config_path UTF-8 部署配置文件路径，最多读取 1 MiB。
     * @param[in] ctx 初始化预算；第三方同步调用受各自超时配置限制。
     * @return 独占拥有的客户端；配置错误/未知后端返回明确错误，不自动降级。
     */
    static Result<std::unique_ptr<RealtimeClient>> open(const std::string& config_path,
                                                        const CallContext& ctx);

    /**
     * @brief 供唯一部署初始化者创建或验证实时后端，不清空已存在的段。
     * @param[in] config_path UTF-8 部署配置文件路径。
     * @param[in] ctx 初始化预算。
     * @return 独占拥有的客户端；调用方负责保证初始化角色及部署唯一性。
     */
    static Result<std::unique_ptr<RealtimeClient>> initialize(const std::string& config_path,
                                                              const CallContext& ctx);

    /** @brief 释放本进程资源；共享内存不随客户端销毁而删除。 */
    ~RealtimeClient();

    /**
     * @brief 读取同一版本的完整记录副本。
     * @param[in] key 逻辑记录身份。
     * @param[in] ctx 调用预算。
     * @return 记录副本或明确错误；不存在不等于零值。
     */
    Result<Record> read(const RecordKey& key, const CallContext& ctx);

    /**
     * @brief 按预期版本完整替换或条件创建记录。
     * @param[in] request 键、新字段及预期 revision。
     * @param[in] ctx 调用预算。
     * @return 新 revision；冲突/结果未知保留写入确定性。
     */
    Result<WriteReceipt> compareExchange(const WriteRequest& request, const CallContext& ctx);

    /**
     * @brief 按预期版本删除记录。
     * @param[in] request 键和预期 revision。
     * @param[in] ctx 调用预算。
     * @return 删除凭据或带写入确定性的错误。
     */
    Result<EraseReceipt> erase(const EraseRequest& request, const CallContext& ctx);

    /**
     * @brief 逐项批量读取，不保证整批快照。
     * @param[in] keys 逻辑记录键列表。
     * @param[in] ctx 整次调用共用的预算。
     * @return 与输入等长、同序的逐项结果。
     */
    std::vector<Result<Record>> readMany(const std::vector<RecordKey>& keys,
                                         const CallContext& ctx);

    /**
     * @brief 逐项批量条件写入，不提供跨记录事务。
     * @param[in] requests 条件写入列表。
     * @param[in] ctx 整次调用共用的预算。
     * @return 与输入等长、同序的逐项结果。
     */
    std::vector<Result<WriteReceipt>> writeMany(const std::vector<WriteRequest>& requests,
                                                const CallContext& ctx);

    /**
     * @brief 返回不进行网络探测的运维能力快照。
     * @return 后端能力与存储代次；业务算法不应据此选择后端分支。
     */
    BackendInfo info() const;

    /** @brief 停止调用后幂等关闭本进程资源，不删除后端。 */
    void close() noexcept;

   private:
    struct Impl;
    /**
     * @brief 接管已完成初始化的实现。
     * @param[in] impl 实现对象，所有权转交本客户端。
     */
    explicit RealtimeClient(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::realtime
