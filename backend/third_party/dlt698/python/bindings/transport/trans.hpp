/** @file trans.hpp
 * @brief 透明转发的有界原生请求桥；由 Python 调用线程取出并完成。
 */
#pragma once
#include <dlt698/service/advanced.hpp>
#include <map>
#include <mutex>

namespace dlt698::python {
struct TransJob {
    std::uint64_t token;
    protocol::apdu::ProxyTransRequest request;
};

/** @brief 透明端口异步桥，不持有 Python 回调或依赖 Python 后台线程。
 * @note 应用 drain 后必须在期限内完成；关闭、超时或取消后的重复完成返回 false。
 */
class TransBridge : public std::enable_shared_from_this<TransBridge> {
   public:
    /** @brief 设置待完成请求与命令字节总预算。
     * @param[in] max_pending 非零请求上限。
     * @param[in] max_bytes 非零命令字节预算。
     * @throws std::invalid_argument 预算为零。
     */
    TransBridge(std::size_t max_pending = 128, std::size_t max_bytes = 1024 * 1024);
    /** @brief 关闭未完成请求；不调用 Python。 */
    ~TransBridge();
    /** @brief 取出当前未分发请求的拥有型快照。
     * @return 每个 token 只分发一次；取消的请求不返回。
     */
    std::vector<TransJob> drain();
    /** @brief 完成一个请求，执行原生处理器时不持有桥的锁。
     * @param[in] token 待完成请求标识。
     * @param[in] result 拥有型成功响应或完整错误。
     * @return 首次完成返回 true，已取消/完成/关闭返回 false。
     * @throws std::invalid_argument 成功响应端口与请求不同，请求仍可重试完成。
     */
    bool complete(std::uint64_t token, Result<protocol::apdu::ProxyTransResponse> result);
    /** @brief 幂等关闭并以 closed 完成所有待处理请求。 */
    void close();
    /** @brief 提交原生异步透明操作，供 Engine 的 AdvancedService 调用。
     * @param[in] request 拥有型命令。
     * @param[in] handler 原生完成处理器，不得同步调用 Python。
     * @return 非阻塞幂等取消函数；超预算立即以 resource_limit 完成。
     */
    service::IProxyProvider::Cancel submit(
        protocol::apdu::ProxyTransRequest request,
        std::function<void(Result<protocol::apdu::ProxyTransResponse>)> handler);

   private:
    struct Pending {
        protocol::apdu::ProxyTransRequest request;
        std::function<void(Result<protocol::apdu::ProxyTransResponse>)> handler;
        bool dispatched = false;
    };

    /** @brief 取消请求并释放预算，取消不调用完成处理器。
     * @param[in] token 已由上层超时或关闭结束的请求。
     */
    void cancel(std::uint64_t token);
    std::mutex mutex_;
    std::map<std::uint64_t, Pending> pending_;
    std::size_t max_pending_, max_bytes_, bytes_ = 0;
    std::uint64_t next_ = 1;
    bool closed_ = false;
};
}  // namespace dlt698::python
