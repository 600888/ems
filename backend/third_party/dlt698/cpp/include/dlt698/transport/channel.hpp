/**
 * @file channel.hpp
 * @brief 异步字节流通道接口及完成回调类型。
 */
#pragma once
#include <dlt698/common/bytes.hpp>
#include <dlt698/export.hpp>
#include <functional>

namespace dlt698::transport {
/// 异步字节流通道；读取的字节块不保证与协议帧边界一致。
// 必须整类导出：跨动态库派生类需要基类的 typeinfo 和 vtable，
// 只导出成员函数时这两个符号在 ELF/Mach-O 上仍是隐藏的，链接会失败。
class DLT698_API IChannel {
   public:
    using ReadHandler = std::function<void(Result<Bytes>)>;  ///< 读取完成，成功值拥有缓冲区。
    using WriteHandler = std::function<void(Result<void>)>;  ///< 整个写操作完成或失败。
    /** @brief 通过接口销毁具体通道实现。 */
    virtual ~IChannel() = default;
    /**
     * @brief 异步读取一个非空字节块，每个通道最多允许一个在途读取。
     * @param[in] handler 完成回调，接收拥有内存的字节块或错误；重复读取返回 busy。
     * @note 字节块可能只含半帧或同时包含多帧，调用方须自行进行流解析。
     */
    virtual void async_read(ReadHandler handler) = 0;
    /**
     * @brief 接管缓冲区并异步写入全部字节。
     * @param[in] bytes 待发送字节，所有权移入通道。
     * @param[in] handler 完成回调，成功表示全部字节已写入，失败返回错误信息。
     */
    virtual void async_write(Bytes bytes, WriteHandler handler) = 0;
    /**
     * @brief 幂等关闭通道，使挂起操作以错误完成。
     * @note 异步实现需要继续驱动其运行时，才能处理关闭和完成回调。
     */
    virtual void close() = 0;
};
}  // namespace dlt698::transport
