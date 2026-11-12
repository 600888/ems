/** @file detail.hpp
 * @brief TCP/串口共用的运行时、错误转换与投递前写预算。
 */
#pragma once
#include <asio.hpp>
#include <dlt698/transport/tcp.hpp>
#include <mutex>

namespace dlt698::transport {
struct IoRuntime::Impl {
    asio::io_context context;
    // 没有隐藏工作线程，工作守卫只维持应用主动驱动的空闲事件循环。
    asio::executor_work_guard<asio::io_context::executor_type> work{asio::make_work_guard(context)};
    std::mutex work_mutex;
};

namespace detail {
/** @brief 转换操作系统 I/O 错误。
 * @param[in] ec Asio 原始错误。
 * @return 保留错误描述的取消、关闭或 I/O 错误。
 */
inline Error io_error(const asio::error_code& ec) {
    return {ec == asio::error::operation_aborted ? ErrorCode::cancelled
            : ec == asio::error::eof             ? ErrorCode::closed
                                                 : ErrorCode::io_error,
            0, ec.message()};
}

/** @brief 隔离用户完成回调异常。
 * @tparam H 回调类型。
 * @tparam R 结果类型。
 * @param[in,out] handler 完成回调。
 * @param[in] result 移交给回调的结果。
 */
template <class H, class R>
void deliver(H& handler, R result) noexcept {
    try {
        if (handler) handler(std::move(result));
    } catch (...) {
    }
}

/** @brief 检查原始通道资源配置。
 * @param[in] options 待读取分块与写预算配置。
 * @throws std::invalid_argument 读取块不在 1 至 1 MiB 或写预算为零。
 */
inline void validate(const ChannelOptions& options) {
    if (!options.read_chunk_bytes || options.read_chunk_bytes > 1024 * 1024 ||
        !options.max_pending_write_bytes || !options.max_pending_writes)
        throw std::invalid_argument("invalid channel options");
}

struct WriteBudget {
    /** @brief 创建写预算计数器。
     * @param[in] value 已校验的通道配置。
     */
    explicit WriteBudget(ChannelOptions value) : options(value) {}

    ChannelOptions options;
    std::mutex mutex;
    std::size_t bytes = 0, count = 0;

    /** @brief 在投递到 strand 之前线程安全预占预算，空写仍计一条。
     * @param[in] size 此操作字节数。
     * @return 未超过字节和条数上限时为 true，否则不更改预算。
     */
    bool acquire(std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        if (count >= options.max_pending_writes || size > options.max_pending_write_bytes - bytes)
            return false;
        bytes += size;
        ++count;
        return true;
    }

    /** @brief 在用户回调之前释放已成功预占的预算，允许回调重入写入。
     * @param[in] size 与 acquire 成功时相同的字节数。
     * @pre 每次成功预占仅释放一次。
     */
    void release(std::size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        bytes -= size;
        --count;
    }
};
}  // namespace detail
}  // namespace dlt698::transport
