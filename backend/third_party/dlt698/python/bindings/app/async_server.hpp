/** @file async_server.hpp
 * @brief C++ 托管服务端启停的异步完成桥；Python 只轮询通知，不创建线程。
 */
#pragma once
#include <atomic>
#include <dlt698/app/server.hpp>
#include <functional>
#include <optional>
#include <thread>

namespace dlt698::python {
struct ServerOperation {
    std::uint64_t token = 0;
    std::optional<Error> error;
};

/** @brief 复用 app::Server 的原生运行线程，额外由 C++ 管理启停等待线程。
 * @note 提交和 poll 必须从构造线程调用；同一时刻只接受一个操作。
 * 线程不持有 Python 对象或调用 Python；析构等待尚未完成的操作并停止服务器。
 * 提交函数跨线程或上次操作尚未取出时抛 std::runtime_error，创建线程失败抛
 * std::system_error；原生业务错误通过 ServerOperation.error 返回，不自动重试。
 */
class ServerRunner {
   public:
    /** @brief 共享拥有已构造的原生服务器，不立即创建线程。
     * @param[in] server 非空服务器；由桥及 Python 句柄共同持有。
     * @throws std::invalid_argument 服务器为空。
     */
    explicit ServerRunner(std::shared_ptr<app::Server> server);
    /** @brief 等待管理操作退出并停止原生运行；不得在持有 GIL 时销毁。 */
    ~ServerRunner();
    /** @brief 异步提交 TCP 监听启动。
     * @param[in] address 数字监听地址。
     * @param[in] port 监听端口，零自动分配。
     * @param[in] profile 关联场景。
     * @return 操作标识；结果由 poll 取出。
     */
    std::uint64_t start_tcp(std::string address, std::uint16_t port,
                            app::ConnectionProfile profile);
    /** @brief 异步提交默认 8E1 串口启动。
     * @param[in] path 串口路径。
     * @param[in] baud 波特率。
     * @param[in] profile 关联场景。
     * @return 操作标识；结果由 poll 取出。
     */
    std::uint64_t start_serial(std::string path, unsigned baud, app::ConnectionProfile profile);
    /** @brief 异步提交完整串口配置启动。
     * @param[in] path 串口路径。
     * @param[in] serial 字格式、流控与通道预算。
     * @param[in] link 串行帧间隔配置。
     * @param[in] profile 关联场景。
     * @return 操作标识；结果由 poll 取出。
     */
    std::uint64_t start_serial_configured(std::string path, transport::SerialOptions serial,
                                          transport::SerialLinkOptions link,
                                          app::ConnectionProfile profile);
    /** @brief 异步等待原生服务器停止，保留设备数据。
     * @return 操作标识；结果由 poll 取出。
     */
    std::uint64_t stop();
    /** @brief 非阻塞查询已经完成的操作并回收已退出的 C++ 管理线程。
     * @return 至多一条完成结果；待完成时返回空。
     * @throws std::runtime_error 非构造线程调用。
     */
    std::optional<ServerOperation> poll();

   private:
    /** @brief 检查提交与轮询的线程归属。 */
    void check() const;
    /** @brief 用 C++ 线程执行原生操作并保存拥有型结果。
     * @param[in] operation 不访问 Python 的原生操作。
     * @return 单调递增操作标识。
     * @throws std::runtime_error 上次完成未取出或操作尚未退出。
     */
    std::uint64_t submit(std::function<Result<void>()> operation);

    std::shared_ptr<app::Server> server_;
    std::thread::id owner_ = std::this_thread::get_id();
    std::thread worker_;
    std::atomic<bool> finished_{false};
    std::optional<ServerOperation> completion_;
    std::uint64_t next_token_ = 1;
};
}  // namespace dlt698::python
