/**
 * @file i_event_bus.h
 * @brief 事件总线接口（design.md 第 10.2 节）
 *
 * publish 返回 PublishReceipt（仅表示后端接受）；read 返回有界
 * Delivery 列表（可重复投递，无全局顺序承诺）；ack 幂等且不等于
 * 数据库归档证明。对 publish/ack 超时，若无法确定结果则返回
 * Unknown（effect=Unknown），重试使用同 event_id。
 *
 * 不能依赖事件总线实现设备动作恰好一次。阻塞消费连接独立于
 * 实时读写池；读批次/载荷/并发/重试全部有上限。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/messaging/delivery.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace ems::messaging {

class IEventBus {
   public:
    virtual ~IEventBus() = default;

    /// 发布事件；成功返回传输游标，仅表示后端接受
    virtual core::Result<PublishReceipt> publish(const std::string& topic, const std::string& scope,
                                                 const EventEnvelope& event,
                                                 const core::CallContext& ctx) = 0;

    /// 有界读取；可重复投递，超过 max_items/max_bytes 截断
    virtual core::Result<std::vector<Delivery>> read(const Subscription& subscription,
                                                     std::uint64_t max_items,
                                                     std::uint64_t max_bytes,
                                                     const core::CallContext& ctx) = 0;

    /// 确认投递；重复确认幂等；调用者 ACK 前必须完成其职责要求的持久提交
    virtual core::Status ack(const Subscription& subscription, const std::string& delivery_token,
                             const core::CallContext& ctx) = 0;

    /// 接管待处理消息（最小空闲时间、数量上限）；可能与旧消费者重复处理
    virtual core::Result<std::uint64_t> reclaim(const Subscription& subscription,
                                                std::chrono::milliseconds min_idle,
                                                std::uint64_t max_items,
                                                const core::CallContext& ctx) = 0;

    /// 停止新调用并结束阻塞读取；未确认消息留待恢复。幂等。
    virtual core::Status close(const core::CallContext& ctx) = 0;
};

}  // namespace ems::messaging
