#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/messaging/delivery.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ems::storage {

struct FeedStatistics {
    std::uint64_t received{0};
    std::uint64_t dropped_local{0};
};

/** @brief 存储进程的原始遥测接收接口；投递只在本进程内缓冲，不提供可靠重放。 */
class IHistoryFeed {
   public:
    /** @brief 销毁遥测接收接口。 */
    virtual ~IHistoryFeed() = default;

    /**
     * @brief 从本地有界缓冲取出一批原始遥测，超出单批字节预算的帧丢弃，取出后不重投。
     * @param[in] max_items 最大消息数。
     * @param[in] max_bytes 最大载荷字节数。
     * @param[in] ctx 等待截止时间。
     * @return 本次取得的投递；读取错误返回错误状态。
     */
    virtual core::Result<std::vector<messaging::Delivery>> read(std::uint64_t max_items,
                                                                std::uint64_t max_bytes,
                                                                const core::CallContext& ctx) = 0;

    /**
     * @brief 读取本进程已接收和本地溢出或关闭丢弃计数。
     * @return 计数快照；不包含网络中或发布端丢失的消息。
     */
    virtual FeedStatistics stats() const = 0;

    /** @brief 幂等停止接收并释放 ZeroMQ 资源。 */
    virtual void close() noexcept = 0;
};

struct ZmqTelemetryOptions {
    std::string endpoint{"tcp://127.0.0.1:5559"};
    std::string topic;
    std::size_t max_queue_items{1024};
    std::size_t max_message_bytes{65536};
    int high_water_mark{1024};
};

/**
 * @brief 订阅原始遥测事件的完整 ZeroMQ 主题。
 * @param[in] options 端点、完整主题及有界接收参数。
 * @return 成功时返回进程私有接收接口；初始化失败返回错误。
 */
core::Result<std::unique_ptr<IHistoryFeed>> connect_zmq_telemetry(
    const ZmqTelemetryOptions& options);

}  // namespace ems::storage
