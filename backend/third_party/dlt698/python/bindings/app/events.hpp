/** @file events.hpp
 * @brief 原生线程仅复制事件到有界队列，不访问解释器。
 */
#pragma once
#include <deque>
#include <dlt698/session/traffic.hpp>
#include <mutex>

#include "bindings.hpp"

namespace dlt698::python {
struct Event {
    std::string kind;
    std::uint64_t connection = 0;
    Bytes bytes;
    std::optional<Error> error;
    double timestamp = 0;
};

class EventQueue {
   public:
    /** @brief 创建有界队列。
     * @param[in] capacity 事件数量上限，必须非零。
     * @param[in] byte_limit 累积字节上限，必须非零。
     * @throws std::invalid_argument 预算为零。
     */
    EventQueue(std::size_t capacity = 256, std::size_t byte_limit = 1024 * 1024)
        : capacity_(capacity), byte_limit_(byte_limit) {
        if (!capacity || !byte_limit) throw std::invalid_argument("event queue budget is zero");
    }

    /** @brief 从原生线程复制事件；满队列丢弃新事件并增加计数，不阻塞 I/O。
     * @param[in] event 拥有全部字节和诊断内容的事件。
     */
    void push(Event event) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (queue_.size() >= capacity_ || event.bytes.size() > byte_limit_ - bytes_) {
            ++dropped_;
            return;
        }
        bytes_ += event.bytes.size();
        queue_.push_back(std::move(event));
    }

    /** @brief 在调用线程取出最多指定数量的事件。
     * @param[in] count 本次最多取出的数量。
     * @return 拥有型事件列表，顺序与入队顺序一致。
     */
    std::vector<Event> drain(std::size_t count = 256) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Event> output;
        while (count-- && !queue_.empty()) {
            bytes_ -= queue_.front().bytes.size();
            output.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        return output;
    }

    /** @brief 停止接受事件并丢弃待分发事件，后续不会回调 Python。 */
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        queue_.clear();
        bytes_ = 0;
    }

    /** @brief 查询累计丢弃数量。
     * @return 原生线程入队时超限的事件数。
     */
    std::size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

   private:
    mutable std::mutex mutex_;
    std::deque<Event> queue_;
    std::size_t capacity_, byte_limit_, bytes_ = 0, dropped_ = 0;
    bool closed_ = false;
};

/** @brief 将借用的会话观察事件复制成安全快照。
 * @param[in] connection 连接标识。
 * @param[in] traffic 本次回调内有效的原生事件。
 * @return owning 事件，时间以 Unix 秒表示。
 */
inline Event traffic_event(std::uint64_t connection, const session::TrafficEvent& traffic) {
    Event result;
    result.kind = traffic.direction == session::TrafficDirection::receive ? "receive" : "send";
    result.connection = connection;
    if (traffic.bytes.size())
        result.bytes.assign(traffic.bytes.data(), traffic.bytes.data() + traffic.bytes.size());
    result.timestamp = std::chrono::duration<double>(traffic.timestamp.time_since_epoch()).count();
    if (!traffic.result) result.error = traffic.result.error();
    return result;
}
}  // namespace dlt698::python
