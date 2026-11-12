/** @file serial_link.hpp
 * @brief 可在内存或原始串口通道上验证的 698 串行链路适配。
 */
#pragma once
#include <dlt698/common/executor.hpp>
#include <dlt698/transport/channel.hpp>

namespace dlt698::transport {
struct SerialLinkOptions {
    unsigned baud_rate = 9600;
    unsigned bits_per_character = 11;  ///< 含起始、数据、校验、停止位；须与实际端口一致。
    std::size_t max_pending_write_bytes = 1024 * 1024;
    std::size_t max_pending_writes = 128;
    std::function<Result<void>(bool)> set_transmit;  ///< 幂等设置方向：true 发送，false 接收。
    std::function<void(IChannel::WriteHandler)>
        async_drain;  ///< 驱动确认最后停止位已经发出，再完成回调。
};

/** @brief 为每帧加四个 FE，保留至少 33 位时间的收发间隔。
 * @note 有方向切换时必须提供真实 async_drain。没有排空回调时仅以字节/波特率估算
 * 输出时间，适合自动方向适配器；OS/USB 队列和真实硬件时序仍需现场验证。
 */
class SerialLinkChannel final : public IChannel {
   public:
    /** @brief 包装已打开的原始通道。
     * @param[in] raw 原始通道，共享所有权；关闭适配器同时关闭它。
     * @param[in] executor 串行执行器，须由应用持续驱动。
     * @param[in] options 实际线路参数、队列预算及可选驱动 hooks。
     * @return 已创建适配器。
     * @throws std::invalid_argument 参数非法或配置方向切换但没有排空回调。
     */
    DLT698_API static std::shared_ptr<SerialLinkChannel> wrap(std::shared_ptr<IChannel> raw,
                                                              std::shared_ptr<IExecutor> executor,
                                                              SerialLinkOptions options = {});
    /** @brief 关闭原始通道，未完成写操作以 closed 结束；仍须驱动执行器。 */
    DLT698_API ~SerialLinkChannel() override;
    /** @brief 禁止复制发送队列。 */
    SerialLinkChannel(const SerialLinkChannel&) = delete;
    /** @brief 禁止复制赋值链路状态。 */
    SerialLinkChannel& operator=(const SerialLinkChannel&) = delete;
    /** @brief 读取原始字节并记录最近接收时间，不剥离接收 FE。
     * @param[in] handler 完成回调，字节由上层流解析器处理。
     */
    DLT698_API void async_read(ReadHandler handler) override;
    /** @brief 排队发送恰好一个含校验的完整帧，添加 FE 后全量发送。
     * @param[in] bytes 不含 FE 的链路帧，接管所有权；不接受半帧或多帧拼接。
     * @param[in] handler 排空/估算时间及帧间隔结束后完成；预算含 FE，失败不自动重试。
     */
    DLT698_API void async_write(Bytes bytes, WriteHandler handler) override;
    /** @brief 幂等关闭，取消定时器和队列，并尽力恢复接收方向。 */
    DLT698_API void close() override;

   private:
    struct Impl;
    /** @brief 包装已配置的实现。
     * @param[in] impl 非空内部实现。
     */
    explicit SerialLinkChannel(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::transport
