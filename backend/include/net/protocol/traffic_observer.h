#pragma once

#include <chrono>
#include <mutex>

#include "net/protocol/protocol_plugin_api.h"

namespace ems::net::protocol {

/** @brief 隔离观察异常并同步注销；独立于协议锁，回调不能重入观察器。 */
class TrafficObserver {
   public:
    /**
     * @brief 安装或注销观察回调，等待旧回调结束后返回。
     * @param[in] callback 回调，空表示关闭观察。
     * @param[in,out] user 借用的宿主上下文。
     */
    void set(ems_plugin_traffic_callback callback, void* user) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = callback;
        user_ = user;
    }

    /**
     * @brief 在当前线程观察收发字节，异常不能影响通信。
     * @param[in] direction 收发方向及发送失败标记。
     * @param[in] data 借用的字节缓冲。
     * @param[in] size 字节数，空缓冲不通知。
     * @param[in] connection_id 服务端连接标识；0 表示未区分。
     * @param[in] timestamp_us UTC 微秒时标；0 表示在入口采集。
     */
    void emit(int direction, const uint8_t* data, size_t size, uint64_t connection_id = 0,
              int64_t timestamp_us = 0) noexcept {
        if (!data || !size) return;
        if (!timestamp_us)
            timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (callback_) callback_(user_, direction, data, size, timestamp_us, connection_id);
        } catch (...) {
        }
    }

   private:
    std::mutex mutex_;
    ems_plugin_traffic_callback callback_{nullptr};
    void* user_{nullptr};
};
}  // namespace ems::net::protocol
