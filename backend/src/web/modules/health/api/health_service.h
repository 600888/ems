#pragma once

#include <atomic>
#include <cstdint>

#include "web/transport/json.h"

namespace ems::web {
class HealthService {
   public:
    /** @brief 设置业务准入就绪状态。
     * @param[in] value 是否可接收新业务。
     */
    void set_ready(bool value);
    /** @brief 查询本进程准入状态。
     * @return 当前是否就绪，不代表现场设备在线。
     */
    bool ready() const;
    /** @brief 记录一次已回复请求，不记录原始业务参数。
     * @param[in] status HTTP 状态。
     * @param[in] elapsed_us 从准入到回复的耗时。
     */
    void record(int status, std::uint64_t elapsed_us);
    /** @brief 读取脱敏进程指标。
     * @return 累计请求、错误、过载与平均时延，大计数用文本。
     */
    Json metrics() const;

   private:
    std::atomic<bool> ready_{false};
    std::atomic<std::uint64_t> completed_{0}, errors_{0}, rejected_{0}, elapsed_us_{0};
};
}  // namespace ems::web
