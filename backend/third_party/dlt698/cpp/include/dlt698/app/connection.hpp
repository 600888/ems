/** @file connection.hpp
 * @brief 托管客户端和服务器共用的协议关联场景。
 */
#pragma once

namespace dlt698::app {
/// 两端必须使用相同场景；远程先 LINK，本地普通先 CONNECT，预设跳过 CONNECT。
enum class ConnectionProfile { remote_public, local_public, local_preset };
}  // namespace dlt698::app
