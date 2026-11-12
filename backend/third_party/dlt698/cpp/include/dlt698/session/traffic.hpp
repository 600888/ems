/** @file traffic.hpp
 * @brief 会话通道边界的收发观察事件与回调类型。
 */
#pragma once
#include <chrono>
#include <dlt698/common/bytes.hpp>
#include <functional>

namespace dlt698::session {
/// 相对于当前会话的字节传输方向。
enum class TrafficDirection { receive, send };

/** @brief 一次读取成功或一次写入完成的观察事件。
 * @note 事件和字节视图仅在回调期间有效；保存日志或跨线程处理须复制字节。
 * RX 在流解析前通知，保留半帧、粘帧、噪声、损坏帧及接收到的 FE 前导字节。
 * TX 是交给会话通道的完整链路帧，不含 SerialLinkChannel 后加的 FE；每次重发单独通知。
 * 时间在通道完成回调入口采集，使用系统时钟，可能受校时影响，不代表物理线路时刻。
 */
struct TrafficEvent {
    TrafficDirection direction;
    std::chrono::system_clock::time_point timestamp;
    ByteView bytes;       ///< RX 为读出的字节块，TX 为提交的全部字节，包括失败写入。
    Result<void> result;  ///< RX 恒成功；TX 是通道写入结果，失败时可能已有部分字节发出。
};

/// 在会话串行执行器内调用，不得阻塞或强引用会话形成环；异常被隔离。
using TrafficHandler = std::function<void(const TrafficEvent&)>;
}  // namespace dlt698::session
