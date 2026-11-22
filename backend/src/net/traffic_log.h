#pragma once

#include <algorithm>
#include <string>
#include <utility>

#include "net/log.h"
#include "net/protocol/protocol_plugin_api.h"

/** @brief 将插件借用的原始字节复制为通道日志消息；须在注销回调后销毁。 */
class ChannelTrafficLog {
   public:
    /**
     * @brief 预先创建通道日志器，避免首次报文回调内创建文件。
     * @param[in] channel 通道名称，与通道状态日志使用相同目录。
     * @param[in] protocol 协议名称。
     */
    ChannelTrafficLog(const std::string& channel, std::string protocol)
        : protocol_(std::move(protocol)), logger_(ChannelMsgLogger::getChannelMsgLogger(channel)) {}

    /**
     * @brief 按协议和方向写入十六进制报文，末尾附总长度与偏移；异常不影响通信。
     * @param[in,out] user 本日志上下文，须覆盖回调生命周期。
     * @param[in] direction EmPluginTrafficDirection。
     * @param[in] data 回调期间借用的原始字节。
     * @param[in] size 字节数。
     * @param[in] timestamp_us UTC 微秒观察时标，保留回调参数但不写入日志。
     * @param[in] connection_id 服务端连接标识，保留回调参数但不写入日志。
     */
    static void EMS_PLUGIN_CALL callback(void* user, int direction, const uint8_t* data,
                                         size_t size, [[maybe_unused]] int64_t timestamp_us,
                                         [[maybe_unused]] uint64_t connection_id) noexcept {
        if (!user || !data || !size) return;
        auto& self = *static_cast<ChannelTrafficLog*>(user);
        try {
            if (!self.logger_) return;
            static constexpr char digits[] = "0123456789ABCDEF";
            constexpr size_t chunk_size = 1024;
            for (size_t offset = 0; offset < size; offset += chunk_size) {
                const auto length = std::min(chunk_size, size - offset);
                std::string hex;
                hex.reserve(length * 3);
                for (size_t i = 0; i < length; ++i) {
                    const auto byte = data[offset + i];
                    if (i) hex += ' ';
                    hex += digits[byte >> 4];
                    hex += digits[byte & 15];
                }
                self.logger_->log(LV::info, "[{}][{}] {} (length={},offset={})", self.protocol_,
                                  direction == EMS_TRAFFIC_RX   ? "RX"
                                  : direction == EMS_TRAFFIC_TX ? "TX"
                                                                : "TX_FAILED",
                                  hex, size, offset);
            }
        } catch (...) {
        }
    }

   private:
    std::string protocol_;
    std::shared_ptr<spdlog::logger> logger_;
};
