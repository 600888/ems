/** @file serial.hpp
 * @brief Asio 串口原始字节通道，链路前导及帧间隔由 SerialLinkChannel 适配。
 */
#pragma once
#include <dlt698/transport/tcp.hpp>

namespace dlt698::transport {
enum class SerialParity { none, odd, even };
enum class SerialStopBits { one, one_point_five, two };
enum class SerialFlowControl { none, software, hardware };

struct SerialOptions {
    unsigned baud_rate = 9600;
    unsigned data_bits = 8;
    SerialParity parity = SerialParity::even;
    SerialStopBits stop_bits = SerialStopBits::one;
    SerialFlowControl flow_control = SerialFlowControl::none;
    ChannelOptions channel;
};

/** @brief 异步原始串口，默认 9600/8E1；不自动重试、设置操作超时或切换 RS-485 方向。 */
class SerialChannel final : public IChannel {
   public:
    /** @brief 同步打开并配置串口，后续读写由运行时驱动。
     * @param[in] runtime 非空 I/O 运行时。
     * @param[in] device Windows COM 名称或 POSIX 设备路径，例如 COM3、/dev/ttyUSB0。
     * @param[in] options 速率、字格式、流控和通道预算。
     * @return 已打开通道或参数/操作系统错误；失败自动释放已打开的端口。
     */
    DLT698_API static Result<std::shared_ptr<SerialChannel>> open(
        std::shared_ptr<IoRuntime> runtime, const std::string& device, SerialOptions options = {});
    /** @brief 释放串口，挂起完成仍须驱动运行时。 */
    DLT698_API ~SerialChannel() override;
    /** @brief 禁止复制端口所有权。 */
    SerialChannel(const SerialChannel&) = delete;
    /** @brief 禁止复制赋值端口。 */
    SerialChannel& operator=(const SerialChannel&) = delete;
    /** @brief 读取任意非空字节块。
     * @param[in] handler 完成回调；同时第二个读取返回 busy。
     */
    DLT698_API void async_read(ReadHandler handler) override;
    /** @brief 保序全量写入原始字节，不添加 FE。
     * @param[in] bytes 操作接管的缓冲区。
     * @param[in] handler 全部交给驱动后返回，不等价于物理线路已经发送完毕；预算不足返回 resource_limit。
     */
    DLT698_API void async_write(Bytes bytes, WriteHandler handler) override;
    /** @brief 幂等关闭串口，结束挂起队列。 */
    DLT698_API void close() override;

   private:
    struct Impl;
    /** @brief 包装成功打开的端口。
     * @param[in] impl 非空内部实现。
     */
    explicit SerialChannel(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::transport
