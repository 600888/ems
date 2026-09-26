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

/**
 * @brief 从当前 steady_clock 时间按预算构造截止点。
 * @param[in] budget 调用可用的时间预算。
 * @return 按当前时间和预算计算的截止点。
 */
inline Deadline make_deadline(std::chrono::milliseconds budget) {
    return std::chrono::steady_clock::now() + budget;
}

/**
 * @brief 构造包含截止点和链路标识的调用上下文。
 * @param[in] budget 调用可用的时间预算。
 * @param[in] trace_id 链路追踪标识。
 * @return 构造完成的调用上下文。
 */
inline CallContext make_call_context(std::chrono::milliseconds budget, std::string trace_id = {}) {
    CallContext ctx;
    ctx.deadline = make_deadline(budget);
    ctx.trace_id = std::move(trace_id);
    return ctx;
}

/**
 * @brief 返回剩余预算；负值表示已超期。
 * @param[in] ctx 调用上下文，包含截止时间等信息。
 * @return 剩余的时间预算；负值表示已超期。
 */
inline std::chrono::steady_clock::duration remaining_budget(const CallContext& ctx) {
    return ctx.deadline - std::chrono::steady_clock::now();
}

/**
 * @brief 以毫秒返回剩余预算；负值表示已超期。
 * @param[in] ctx 调用上下文，包含截止时间等信息。
 * @return 剩余的时间预算；负值表示已超期。
 */
inline int64_t remaining_budget_ms(const CallContext& ctx) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(remaining_budget(ctx)).count();
}

/**
 * @brief 判断调用预算是否已耗尽。
 * @param[in] ctx 调用上下文，包含截止时间等信息。
 * @return true 表示条件成立；false 表示不成立。
 */
inline bool has_expired(const CallContext& ctx) {
    return std::chrono::steady_clock::now() >= ctx.deadline;
}

/**
 * @brief 构造零预算上下文，仅供纯内存操作使用。
 * @return 构造完成的调用上下文。
 */
inline CallContext immediate_context() { return make_call_context(std::chrono::milliseconds(0)); }

}  // namespace ems::core
