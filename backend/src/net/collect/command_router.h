#pragma once

#include "channel_thread.h"

namespace ems::net::collect {
/** @brief 命令服务借用的运行目录接口；具体协议连接仍由采集引擎独占管理。 */
class CommandRouter {
   public:
    /** @brief 释放路由接口。 */
    virtual ~CommandRouter() = default;
    /**
     * @brief 查找当前可控的 Modbus/IEC104 客户端连接。
     * @param[in] point_id 稳定命令点 ID。
     * @return 当前连接；无有效路由时为空。
     */
    virtual std::shared_ptr<ChannelThread> command_channel(const std::string& point_id) const = 0;
    /** @brief 获取站根修订号。 @return 当前 config_revision。 */
    virtual std::int64_t config_revision() const = 0;
    /** @brief 查询是否允许执行。 @return 引擎运行中时为 true。 */
    virtual bool is_running() const = 0;
};
}  // namespace ems::net::collect
