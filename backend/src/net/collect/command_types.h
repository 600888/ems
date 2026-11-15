#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ems::net::collect {
/** @brief 已仲裁的单点命令；超时使用 UTC 毫秒，队列内另用单调时钟限制等待。 */
struct PointCommand {
    std::string command_id;
    std::string point_id;
    double value{0};
    std::int64_t expires_at_ms{0};
    std::int64_t config_revision{0};
    bool verify_only{false};
    std::int64_t sent_at_ms{0};
};

/** @brief Acked 只表示协议确认；Succeeded 表示发送后新反馈满足全部必需判据。 */
struct PointCommandResult {
    std::string status;
    std::string reason;
    std::int64_t sent_at_ms{0};
};

/** @brief 发送前必须持久登记；返回 false 时不调用协议写入。 */
using BeforeCommand = std::function<bool()>;
/** @brief 工作线程在完成、过期或取消后恰好调用一次；回调不得阻塞等待本线程。 */
using CommandCompletion = std::function<void(const PointCommandResult&)>;
}  // namespace ems::net::collect
