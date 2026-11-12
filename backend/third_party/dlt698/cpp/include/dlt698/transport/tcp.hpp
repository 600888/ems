/**
 * @file tcp.hpp
 * @brief 由应用驱动的异步 TCP 运行时、连接通道及监听器。
 */
#pragma once
#include <chrono>
#include <dlt698/common/executor.hpp>
#include <dlt698/export.hpp>
#include <dlt698/transport/channel.hpp>
#include <memory>

namespace dlt698::transport {
/**
 * @brief 由应用调用 run/run_for 驱动的 I/O 运行时，不创建内部工作线程。
 * @note 退出前先关闭通道和监听器并处理完成回调，再停止运行时。
 * 销毁前须确保所有外部运行线程已经结束；回调不应阻塞等待同一运行时的操作。
 */
class IoRuntime {
   public:
    /** @brief 创建带工作守卫的 I/O 运行时。 */
    DLT698_API IoRuntime();
    /** @brief 停止运行时，调用方须事先结束外部运行线程。 */
    DLT698_API ~IoRuntime();
    /** @brief 禁止复制运行时及其事件循环。 */
    IoRuntime(const IoRuntime&) = delete;
    /** @brief 禁止通过复制赋值共享事件循环。 */
    IoRuntime& operator=(const IoRuntime&) = delete;
    /**
     * @brief 在当前线程处理 I/O 事件，直到运行时停止。
     * @note 工作守卫使空闲运行时继续等待；多个运行线程间的通道状态由 strand 串行化。
     */
    DLT698_API void run();
    /**
     * @brief 在当前线程处理 I/O 事件，直到时限到达或运行时停止。
     * @param[in] duration 本次驱动事件循环的时间预算，不是单个操作的超时。
     * @note 正在执行的用户回调不会被时限中断。
     */
    DLT698_API void run_for(std::chrono::milliseconds duration);
    /**
     * @brief 请求事件循环停止，使运行调用尽快返回。
     * @note 不关闭通道；未处理的完成回调需要 restart 后继续驱动才能执行。
     */
    DLT698_API void stop();
    /** @brief 释放空闲工作守卫，让 run 在所有排队任务和异步操作完成后自然返回。
     * @note 不关闭通道或取消计时器；调用方须先请求关闭自己拥有的资源。
     * 幂等且可跨线程调用，不丢弃完成回调；释放守卫不可逆，重新托管服务应创建新运行时。
     */
    DLT698_API void finish();
    /**
     * @brief 清除停止状态，允许再次驱动事件循环。
     * @pre 所有 run/run_for 调用均已返回。
     */
    DLT698_API void restart();
    /** @brief 创建由当前运行时驱动的串行会话执行器。
     * @return 独立 strand 上的执行器和单调计时器实现，允许从不同线程投递任务。
     * @note 调用方仍须驱动 run/run_for；执行器会保持运行时内部上下文存活。
     */
    DLT698_API std::shared_ptr<IExecutor> executor();

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend class TcpChannel;
    friend class TcpListener;
    friend class SerialChannel;
};

/// 通道资源配置；读取块长度为 1 至 1 MiB，两个写入上限都必须非零。
struct ChannelOptions {
    std::size_t read_chunk_bytes = 4096;                ///< 单次读取缓冲区大小，不代表协议帧长度。
    std::size_t max_pending_write_bytes = 1024 * 1024;  ///< 所有未完成写操作的总字节上限。
    std::size_t max_pending_writes = 128;               ///< 所有未完成写操作的条数上限。
};
class TcpListener;

/**
 * @brief 异步 TCP 字节流通道，通过独立 strand 串行处理状态和回调。
 * @note 应在连接成功后读写；回调在运行时线程执行，异常被隔离，不自动重试或设置超时。
 * 应用须持有通道至操作完成，内部异步操作不会永久保活通道。
 */
class TcpChannel final : public IChannel {
   public:
    using ConnectHandler = std::function<void(Result<void>)>;
    /**
     * @brief 创建通道并异步解析主机地址、建立 TCP 连接。
     * @param[in] runtime 非空的 I/O 运行时，须由应用持续驱动。
     * @param[in] host IP 地址或待解析的主机名。
     * @param[in] port 远端 TCP 端口。
     * @param[in] handler 连接完成回调，成功后可发起读写。
     * @param[in] options 读取块和未完成写操作的资源配置。
     * @return 立即返回的通道对象，此时连接可能尚未完成。
     * @throws std::invalid_argument 运行时为空或通道配置非法。
     */
    DLT698_API static std::shared_ptr<TcpChannel> connect(std::shared_ptr<IoRuntime> runtime,
                                                          std::string host, std::uint16_t port,
                                                          ConnectHandler handler,
                                                          ChannelOptions options = {});
    /** @brief 释放通道；销毁导致的失败回调仍需驱动运行时才能处理。 */
    DLT698_API ~TcpChannel() override;
    /** @brief 禁止复制 socket 所属通道。 */
    TcpChannel(const TcpChannel&) = delete;
    /** @brief 禁止复制赋值 socket 所属通道。 */
    TcpChannel& operator=(const TcpChannel&) = delete;
    /**
     * @brief 异步读取任意大小的字节块，每次最多读取 read_chunk_bytes 字节。
     * @param[in] handler 完成回调，返回字节块或 closed、busy、I/O 等错误。
     * @note 一次只允许一个在途读取；回调在该通道的 strand 中执行。
     */
    DLT698_API void async_read(ReadHandler handler) override;
    /**
     * @brief 将完整缓冲区加入发送队列，串行执行写操作。
     * @param[in] bytes 移入通道的待发送数据。
     * @param[in] handler 完成回调，返回成功或 closed、resource_limit、I/O 等错误。
     * @note 字节和条数预算包含已投递但尚未进入 strand 的操作，空写也占一条预算。
     * 写操作按进入 strand 的顺序执行，并发调用之间的先后顺序由执行器决定。
     */
    DLT698_API void async_write(Bytes bytes, WriteHandler handler) override;
    /**
     * @brief 投递幂等关闭操作，取消解析和 socket 操作并结束待发送队列。
     * @note 关闭和挂起操作的回调需要应用继续驱动运行时。
     */
    DLT698_API void close() override;

   private:
    struct Impl;
    /**
     * @brief 将已创建的连接实现包装为公开通道。
     * @param[in] impl 由连接工厂或监听器创建的非空实现对象。
     */
    explicit TcpChannel(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
    friend class TcpListener;
};

/**
 * @brief TCP 监听器，串行处理接受操作及完成回调。
 * @note 仅负责 TCP 连接，不限定 DL/T 698 协议角色；回调异常被隔离。
 */
class TcpListener {
   public:
    using AcceptHandler = std::function<void(Result<std::shared_ptr<TcpChannel>>)>;
    /**
     * @brief 同步绑定本地地址并开始 TCP 监听。
     * @param[in] runtime 非空运行时，后续接受操作由应用驱动。
     * @param[in] bind_address 本地 IPv4/IPv6 数字地址，不执行主机名解析。
     * @param[in] port 监听端口；零表示由系统分配，可通过 local_port 查询。
     * @param[in] options 应用于后续接受通道的资源配置。
     * @return 监听器，或运行时为空、地址解析及 socket 操作失败的错误。
     * @throws std::invalid_argument 非空运行时下通道配置非法。
     */
    DLT698_API static Result<std::shared_ptr<TcpListener>> listen(
        std::shared_ptr<IoRuntime> runtime, const std::string& bind_address, std::uint16_t port,
        ChannelOptions options = {});
    /** @brief 使用明确的地址独占策略建立监听，保留原 listen 的默认行为。
     * @param[in] runtime 非空运行时，后续接受操作由应用驱动。
     * @param[in] bind_address IPv4/IPv6 数字地址。
     * @param[in] port 监听端口；零由系统分配。
     * @param[in] options 接受通道的资源配置。
     * @param[in] exclusive_address Windows 上启用 SO_EXCLUSIVEADDRUSE，防止同端口重复监听；
     * POSIX 保持常规 SO_REUSEADDR，未启用 SO_REUSEPORT，内核仍拒绝同地址重复监听。
     * @return 监听器或配置及绑定错误。
     * @throws std::invalid_argument 非空运行时下通道配置非法。
     */
    DLT698_API static Result<std::shared_ptr<TcpListener>> listen(
        std::shared_ptr<IoRuntime> runtime, const std::string& bind_address, std::uint16_t port,
        ChannelOptions options, bool exclusive_address);
    /** @brief 释放监听 socket；挂起接受操作的完成仍需驱动运行时。 */
    DLT698_API ~TcpListener();
    /** @brief 禁止复制监听 socket。 */
    TcpListener(const TcpListener&) = delete;
    /** @brief 禁止通过复制赋值共享监听 socket。 */
    TcpListener& operator=(const TcpListener&) = delete;
    /**
     * @brief 查询监听器实际绑定的本地端口。
     * @return 绑定完成时记录的 TCP 端口号。
     */
    DLT698_API std::uint16_t local_port() const noexcept;
    /**
     * @brief 异步接受一个 TCP 连接，最多允许一个在途接受操作。
     * @param[in] handler 完成回调，返回已连接通道或 closed、busy、I/O 等错误。
     * @note 回调在监听器的 strand 中执行；应用须持有监听器至操作完成。
     */
    DLT698_API void async_accept(AcceptHandler handler);
    /**
     * @brief 投递幂等关闭操作，取消尚未完成的接受请求。
     * @note 不关闭此前已接受的通道，应用须继续驱动运行时以处理完成回调。
     */
    DLT698_API void close();

   private:
    struct Impl;
    /**
     * @brief 将已绑定的监听实现包装为公开监听器。
     * @param[in] impl 已成功初始化的非空监听实现。
     */
    explicit TcpListener(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::transport
