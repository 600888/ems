#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>

#include "net/protocol/protocol_plugin_api.h"

/** @brief 跨插件 ABI 统计真实收发，避免将采样结果误当成原始报文。 */
struct TrafficCapture {
    std::atomic<size_t> rx{0}, tx{0};
    std::atomic<bool> invalid{false};

    /**
     * @brief 将测试观察器安装到真实插件。
     * @param[in] setter 可选扩展函数，本测试要求插件提供。
     * @param[in,out] instance 测试拥有的插件实例。
     */
    void attach(ems_plugin_set_traffic_handler_fn setter, void* instance) {
        if (!setter || setter(instance, callback, this) != 0)
            throw std::runtime_error("traffic observer extension unavailable");
    }

    /** @brief 验证双向观察有效；调用方先完成至少一次实际网络往返。 */
    void expect_duplex() const {
        if (!rx || !tx || invalid)
            throw std::runtime_error("missing RX/TX bytes or invalid traffic metadata");
    }

    /**
     * @brief 记录回调期间有效的原始字节及观察元信息。
     * @param[in,out] user 测试观察器。
     * @param[in] direction 收发方向。
     * @param[in] data 原始字节。
     * @param[in] size 字节数。
     * @param[in] timestamp_us UTC 微秒时标。
     * @param[in] connection_id 服务端连接标识。
     */
    static void EMS_PLUGIN_CALL callback(void* user, int direction, const uint8_t* data,
                                         size_t size, int64_t timestamp_us,
                                         uint64_t connection_id) {
        auto& self = *static_cast<TrafficCapture*>(user);
        if (!data || !size || timestamp_us < 1000000000000000LL || direction < EMS_TRAFFIC_RX ||
            direction > EMS_TRAFFIC_TX_FAILED)
            self.invalid = true;
        if (direction == EMS_TRAFFIC_RX) self.rx += size;
        if (direction == EMS_TRAFFIC_TX) self.tx += size;
        (void)connection_id;
    }
};
