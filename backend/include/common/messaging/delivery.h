/**
 * @file delivery.h
 * @brief 事件投递与订阅类型（design.md 第 10.2 节）
 *
 * Delivery 包含 event、transport_cursor、delivery_token、attempt 信息；
 * Redis Stream ID 只用于传输游标，不能替代 event_id。publish 的
 * PublishReceipt 仅表示后端接受，不表示归档。
 */
#pragma once

#include <string>

#include <common/messaging/event.h>

namespace ems::messaging {

/// 传输游标（如 Redis Stream ID）；仅传输层含义，不替代 event_id
struct TransportCursor {
    std::string value;
    bool empty() const noexcept { return value.empty(); }
    bool operator==(const TransportCursor& other) const noexcept { return value == other.value; }
};

/// 发布回执：仅表示事件后端已接受，不表示归档完成
struct PublishReceipt {
    TransportCursor cursor;
};

/// 订阅起始策略
enum class StartFrom : std::uint8_t {
    RetainedBegin = 0,  ///< 从保留流起点读取（可靠业务新消费组默认）
    Newest        = 1,  ///< 只读新增（必须有已完成的历史基线，并记录切换水位）
    FromCursor    = 2   ///< 从指定游标开始
};

/// 订阅声明：显式声明 group、consumer、scope、topic 和起始策略
struct Subscription {
    std::string group;      ///< 消费组（同组分担，不同组独立消费）
    std::string consumer;   ///< 组内消费者标识
    std::string scope;      ///< 逻辑 scope（unit/station）
    std::string topic;      ///< 逻辑 topic（物理名称由 Redis 键专题映射）
    StartFrom start{StartFrom::RetainedBegin};
    TransportCursor start_cursor;  ///< start == FromCursor 时有效
};

/// 一次投递（可重复投递；无全局顺序承诺）
struct Delivery {
    EventEnvelope event;
    TransportCursor transport_cursor;
    std::string delivery_token;  ///< ack 凭证
    std::uint32_t attempt{0};    ///< 投递尝试次数
};

}  // namespace ems::messaging
