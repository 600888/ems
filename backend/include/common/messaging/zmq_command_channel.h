/**
 * @file zmq_command_channel.h
 * @brief ZeroMQ 命令请求与持久接收确认通道。
 *
 * REQ/REP 应答仅表示采集侧业务回调已处理该命令的接收请求，绝不表示设备已执行。
 * 超时或连接故障返回 Unknown；生产者须用相同 command_id 对账与重试。
 */
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace ems::messaging {

struct ZmqCommandConfig {
    std::string endpoint{"tcp://127.0.0.1:5558"};
    std::size_t max_message_bytes{65536};
    int request_timeout_ms{2000};
    int reply_timeout_ms{500};
    int receive_poll_ms{100};
    int high_water_mark{128};
};

struct ZmqCommandRequest {
    std::string command_id;
    std::string payload_json;
};

enum class ZmqCommandReplyCode {
    Accepted,    ///< 已持久登记并交给执行队列；不代表设备执行成功
    Duplicate,   ///< 同 ID 同载荷已登记，调用方应查询当前状态
    Rejected,    ///< 已明确拒绝；业务回调应持久记录原因
    RetryLater,  ///< 未登记，暂时不可受理
    Unknown,     ///< 客户端不能判断远端是否受理
    Invalid      ///< 本地请求不符合契约，或服务端明确拒绝无效帧
};

struct ZmqCommandReply {
    ZmqCommandReplyCode code{ZmqCommandReplyCode::Unknown};
    std::string reason;
};

/** @brief 每次调用使用独立 REQ socket 的命令发送端，可由多个线程并发调用。 */
class ZmqCommandClient {
   public:
    /**
     * @brief 保存端点与超时配置，不立即建立连接。
     * @param[in] config 命令通道配置。
     */
    explicit ZmqCommandClient(ZmqCommandConfig config);

    /**
     * @brief 向采集侧发送稳定 ID 的命令并等待接收确认。
     * @param[in] request 已持久保存的命令 ID 与版本化 JSON 载荷。
     * @return Accepted/Duplicate 只表示采集侧接收状态；超时返回 Unknown。
     */
    ZmqCommandReply send(const ZmqCommandRequest& request) const;

   private:
    ZmqCommandConfig config_;
};

/** @brief 在独占 REP socket 的线程上接收命令。 */
class ZmqCommandServer {
   public:
    using Handler = std::function<ZmqCommandReply(const ZmqCommandRequest&)>;

    /** @brief 创建尚未启动的命令接收端。 */
    ZmqCommandServer();
    /** @brief 停止接收线程并释放 ZeroMQ 资源。 */
    ~ZmqCommandServer();
    /**
     * @brief 禁止复制接收端。
     * @param[in] other 待复制的源对象。
     */
    ZmqCommandServer(const ZmqCommandServer& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     * @return 当前对象引用；该操作不可调用。
     */
    ZmqCommandServer& operator=(const ZmqCommandServer& other) = delete;

    /**
     * @brief 绑定端点并启动接收线程。
     * @param[in] config 端点、大小和超时配置。
     * @param[in] handler 业务接收回调；Accepted 前必须完成持久登记与幂等检查。
     * @return 端点绑定成功时为 true；不代表业务回调已就绪。
     */
    bool start(const ZmqCommandConfig& config, Handler handler);

    /**
     * @brief 查询接收线程是否仍在运行。
     * @return true 表示接收循环仍在运行。
     */
    bool running() const noexcept;

    /**
     * @brief 返回成功绑定后的实际端点，支持通配端口测试；不可与 stop 并发调用。
     * @return 端点地址；未启动或绑定失败时为空。
     */
    std::string bound_endpoint() const;

    /** @brief 幂等停止；业务回调须有界返回，且不得从回调中调用 stop。 */
    void stop() noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::messaging
