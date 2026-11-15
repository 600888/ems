#pragma once

#include <common/messaging/zmq_command_channel.h>

#include <functional>

namespace ems::web {

/** @brief 命令提交函数：把稳定 ID 与业务载荷转发给采集侧命令服务；返回通道接收确认。 */
using CommandSubmitter =
    std::function<messaging::ZmqCommandReply(const messaging::ZmqCommandRequest&)>;

}  // namespace ems::web
