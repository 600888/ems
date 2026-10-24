#pragma once
#include <rtdb/store/backend_factory.h>

#include <memory>

namespace ems::rtdb {
/** @brief 共享内存段的创建、布局和容量配置。 */
struct SharedMemoryOptions {
    std::string segment_name;
    bool create_if_missing{false};    ///< 仅部署初始化器允许创建新段。
    std::string expected_generation;  ///< 附加到现有段时可选的代次栅栏。
    std::size_t capacity{1024};
    std::size_t shard_count{16};
    std::size_t max_record_bytes{65536};
    std::size_t max_batch_items{256};
    std::size_t segment_bytes{268435456};  ///< 映射容量预算，创建前会校验。
};

/**
 * @brief 使用 Boost.Interprocess 管理段、共享容器和命名锁保存记录。
 *
 * 首批使用段级锁；shard_count 仅参与部署兼容校验。持锁进程崩溃后
 * 可能持续超时，须先隔离所有使用者再受控重建，不自动强制解锁。
 */
class EMS_RTDB_API SharedMemoryStore final : public RealtimeStoreBase {
   public:
    /**
     * @brief 在截止时间前创建或附加共享内存段。
     * @param[in] options 共享内存布局与容量配置。
     * @param[in] ctx 调用上下文，包含截止时间。
     * @return 成功时包含结果；失败时包含错误信息。
     */
    static Result<std::unique_ptr<IRealtimeStore>> open(const SharedMemoryOptions& options,
                                                        const CallContext& ctx);

    /**
     * @brief 通过 Boost 删除共享段及其命名锁；调用方必须先隔离全部使用者。
     * @param[in] segment_name 部署中指定的逻辑共享段名，不接受 OS 路径。
     * @return 成功时返回是否删除了任何资源；非法名称返回 InvalidArgument。
     */
    static Result<bool> remove(const std::string& segment_name);

    /** @brief 释放此实例持有的共享内存资源。 */
    ~SharedMemoryStore() override;

    /**
     * @copydoc IRealtimeStore::info
     * @return 得到的后端信息。
     */
    BackendInfo info() const override;

    /**
     * @copydoc IRealtimeStore::read
     * @param[in] key 待读取的逻辑记录键。
     * @param[in] ctx 调用上下文，包含截止时间。
     * @return 成功时包含记录；失败时包含错误信息。
     */
    Result<Record> read(const RecordKey& key, const CallContext& ctx) override;

    /**
     * @brief 在段级锁内枚举指定作用域的设备状态记录键。
     * @param[in] scope 逻辑作用域。
     * @param[in] ctx 调用预算。
     * @return 至多 65536 个键；超时或超限时返回错误。
     */
    Result<std::vector<RecordKey>> listDeviceStates(const Scope& scope,
                                                    const CallContext& ctx) override;

    /**
     * @copydoc IRealtimeStore::compareExchange
     * @param[in] request 带预期修订号的写入请求。
     * @param[in] ctx 调用上下文，包含截止时间。
     * @return 成功时包含写入凭据；失败时包含错误信息。
     */
    Result<WriteReceipt> compareExchange(const WriteRequest& request,
                                         const CallContext& ctx) override;

    /**
     * @copydoc IRealtimeStore::erase
     * @param[in] request 带预期修订号的删除请求。
     * @param[in] ctx 调用上下文，包含截止时间。
     * @return 成功时包含删除凭据；失败时包含错误信息。
     */
    Result<EraseReceipt> erase(const EraseRequest& request, const CallContext& ctx) override;

    /** @copydoc IRealtimeStore::close */
    void close() noexcept override;

   private:
    struct Impl;
    /**
     * @brief 接管已完成初始化的实现对象。
     * @param[in] impl 已初始化的实现对象，所有权转交给本实例。
     */
    explicit SharedMemoryStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief 向工厂注册共享内存后端。
 * @param[in,out] factory 接收后端注册的工厂。
 * @return 成功时包含结果；失败时包含错误信息。
 */
EMS_RTDB_API Result<bool> register_shared_memory_backend(BackendFactory& factory);
}  // namespace ems::rtdb
