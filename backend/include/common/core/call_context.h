/**
 * @file call_context.h
 * @brief 调用上下文：期限预算与链路标识（docs/common/design.md 第 5.1 节）
 *
 * 所有 I/O 方法接收 CallContext；deadline 必须显式设置（包括获取
 * 池连接/锁的等待时间）。进程内超时使用 steady clock；序列化只传
 * 业务时间或剩余预算，不发送 steady_clock 的绝对数值。
 */
#pragma once

#include <common/core/result.h>

#include <chrono>
#include <string>
#include <utility>

namespace ems::core {

struct CallContext {
    Deadline deadline{};   ///< 调用预算截止点（steady_clock）
    std::string trace_id;  ///< 链路追踪 ID（跨进程传播）
};

/// 用剩余预算构造 deadline
inline Deadline make_deadline(std::chrono::milliseconds budget) {
    return std::chrono::steady_clock::now() + budget;
}

/// 构造带预算的调用上下文
inline CallContext make_call_context(std::chrono::milliseconds budget, std::string trace_id = {}) {
    CallContext ctx;
    ctx.deadline = make_deadline(budget);
    ctx.trace_id = std::move(trace_id);
    return ctx;
}

/// 剩余预算（可能为负，表示已超期）
inline std::chrono::steady_clock::duration remaining_budget(const CallContext& ctx) {
    return ctx.deadline - std::chrono::steady_clock::now();
}

/// 剩余预算（毫秒，负值表示已超期）
inline int64_t remaining_budget_ms(const CallContext& ctx) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(remaining_budget(ctx)).count();
}

/// 预算是否已耗尽
inline bool has_expired(const CallContext& ctx) {
    return std::chrono::steady_clock::now() >= ctx.deadline;
}

/// 无预算上下文仅用于纯内存操作；任何 I/O 都必须显式给出期限
inline CallContext immediate_context() { return make_call_context(std::chrono::milliseconds(0)); }

}  // namespace ems::core
