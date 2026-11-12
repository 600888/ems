/** @file memory.hpp
 * @brief 用于确定性协议模拟的有界内存字节通道。
 */
#pragma once
#include <dlt698/common/executor.hpp>
#include <dlt698/transport/channel.hpp>
#include <memory>
#include <utility>

namespace dlt698::transport {
struct MemoryOptions {
    std::size_t read_chunk_bytes = 4096;
    std::size_t max_buffer_bytes = 1024 * 1024;
    std::size_t max_pending_writes = 128;
};

/** @brief 一对相互连接的内存通道，所有操作在同一串行执行器上排队完成。 */
class MemoryChannel final : public IChannel {
   public:
    /** @brief 创建内存通道对。
     * @param[in] executor 共享串行执行器。
     * @param[in] options 读取分块、每端缓存/待写字节上限及待写操作数上限，均须非零。
     * @return 两个已连接的通道，可分别绑定协议客户机及服务器。
     * @throws std::invalid_argument 执行器为空或配置非法。
     */
    DLT698_API static std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>>
    pair(std::shared_ptr<IExecutor> executor, MemoryOptions options = {});
    /** @brief 关闭本端并唤醒双方尚未完成的读取。 */
    DLT698_API ~MemoryChannel() override;
    /** @brief 禁止复制端点身份。 */
    MemoryChannel(const MemoryChannel&) = delete;
    /** @brief 禁止复制赋值端点身份。 */
    MemoryChannel& operator=(const MemoryChannel&) = delete;
    /** @brief 排队读取一个非空字节块。
     * @param[in] handler 完成回调，第二个在途读取返回 busy。
     */
    DLT698_API void async_read(ReadHandler handler) override;
    /** @brief 复制数据到对端有界缓存，成功表示整块进入缓存。
     * @param[in] bytes 移入操作的数据。
     * @param[in] handler 完成回调，缓存或排队预算不足返回 resource_limit，不执行部分写入。
     */
    DLT698_API void async_write(Bytes bytes, WriteHandler handler) override;
    /** @brief 排队关闭本端，重复关闭没有额外效果。 */
    DLT698_API void close() override;

   private:
    struct Impl;
    /** @brief 绑定共享状态和端点序号。
     * @param[in] impl 通道对共享状态。
     * @param[in] side 端点序号（0/1）。
     */
    MemoryChannel(std::shared_ptr<Impl> impl, unsigned side);
    std::shared_ptr<Impl> impl_;
    unsigned side_;
};
}  // namespace dlt698::transport
