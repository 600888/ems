#pragma once
#include <realtime/backend_factory.h>

#include <memory>

namespace ems::realtime {
/** @brief Redis 连接和实时记录容量配置。 */
struct RedisOptions {
    std::string address{"127.0.0.1"};  ///< redis-plus-plus 端点；建议数值地址以避免阻塞 DNS。
    unsigned port{6379};
    unsigned database{0};
    std::string username;
    std::string password;
    std::size_t pool_size{4};
    std::size_t max_record_bytes{65536};
    std::size_t max_batch_items{256};
    std::size_t transport_timeout_ms{100};  ///< redis-plus-plus 单步连接/socket 超时。
    std::size_t pool_wait_timeout_ms{100};  ///< redis-plus-plus 池借用等待上限。
};

/**
 * @brief 使用 redis-plus-plus 保存实时记录，第三方类型不进入公共头。
 *
 * CallContext 在调用边界检查；同步库调用不能硬取消，其等待还受非零
 * transport_timeout_ms/pool_wait_timeout_ms 约束，可能超过剩余调用预算。
 */
class EMS_RTDB_API RedisStore final : public RealtimeStoreBase {
   public:
    /**
     * @brief 在截止时间前建立 Redis 后端实例。
     * @param[in] options Redis 连接和容量配置。
     * @param[in] ctx 调用上下文，包含截止时间。
     * @return 成功时包含结果；失败时包含错误信息。
     */
    static Result<std::unique_ptr<IRealtimeStore>> open(const RedisOptions& options,
                                                        const CallContext& ctx);

    /** @brief 释放此实例持有的 Redis 资源。 */
    ~RedisStore() override;

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
    explicit RedisStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief 向工厂注册 Redis 后端。
 * @param[in,out] factory 接收后端注册的工厂。
 * @return 成功时包含结果；失败时包含错误信息。
 */
EMS_RTDB_API Result<bool> register_redis_backend(BackendFactory& factory);
}  // namespace ems::realtime
