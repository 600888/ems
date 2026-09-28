/**
 * @file zmq_change_channel.h
 * @brief 与实时存储后端独立的 ZeroMQ 变更通知通道。
 *
 * PUB/SUB 只传递可丢弃的提示。入队成功不代表订阅者收到消息；订阅者必须
 * 从选定的权威实时存储读取快照，不得将通知当作命令或可靠事件。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ems::messaging {

struct ZmqChangeConfig {
    std::string endpoint{"tcp://127.0.0.1:5557"};
    std::size_t max_queue_items{1024};
    std::size_t max_message_bytes{65536};
    int high_water_mark{1024};
};

struct ZmqChangeStats {
    std::uint64_t accepted{0};
    std::uint64_t rejected{0};
    std::uint64_t send_errors{0};
};

/** @brief 将可丢弃的变更提示送入独占 PUB socket 的有界工作线程。 */
class ZmqChangePublisher {
   public:
    /** @brief 创建尚未启动的发布器。 */
    ZmqChangePublisher();
    /** @brief 停止线程并释放 ZeroMQ 资源。 */
    ~ZmqChangePublisher();
    /**
     * @brief 禁止复制发布器。
     * @param[in] other 待复制的源发布器。
     */
    ZmqChangePublisher(const ZmqChangePublisher& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源发布器。
     * @return 当前发布器引用；该操作不可调用。
     */
    ZmqChangePublisher& operator=(const ZmqChangePublisher& other) = delete;

    /**
     * @brief 在工作线程上创建并绑定 PUB socket。
     * @param[in] config 端点、队列和消息大小限制。
     * @return 绑定成功时为 true；失败时可修改配置后重试。
     */
    bool start(const ZmqChangeConfig& config);

    /**
     * @brief 非阻塞地提交变更提示，队列满时丢弃新消息。
     * @param[in] topic ASCII 主题；不得含空格或 NUL，按完整主题精确订阅。
     * @param[in] payload 消息正文，可以包含任意字节。
     * @return true 仅表示本地队列接受；不代表远端收到。
     */
    bool publish(const std::string& topic, const std::string& payload);

    /**
     * @brief 获取本地入队与发送错误计数。
     * @return 计数快照；不能推断订阅者实际收到的数量。
     */
    ZmqChangeStats stats() const;

    /** @brief 幂等停止；丢弃队列内尚未发送的提示。 */
    void stop() noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief 在独占 SUB socket 的线程上接收变更提示。 */
class ZmqChangeSubscriber {
   public:
    using Callback = std::function<void(const std::string&, const std::string&)>;

    /** @brief 创建尚未启动的订阅器。 */
    ZmqChangeSubscriber();
    /** @brief 停止线程并释放 ZeroMQ 资源。 */
    ~ZmqChangeSubscriber();
    /**
     * @brief 禁止复制订阅器。
     * @param[in] other 待复制的源订阅器。
     */
    ZmqChangeSubscriber(const ZmqChangeSubscriber& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源订阅器。
     * @return 当前订阅器引用；该操作不可调用。
     */
    ZmqChangeSubscriber& operator=(const ZmqChangeSubscriber& other) = delete;

    /**
     * @brief 连接端点并订阅指定的完整主题。
     * @param[in] config 端点、接收高水位和消息大小限制。
     * @param[in] topics 完整主题列表，不能为空。
     * @param[in] callback 在接收线程执行的回调；不得在回调内调用 stop。
     * @return socket 初始化成功时为 true；连接成功不表示发布端已在线。
     */
    bool start(const ZmqChangeConfig& config, const std::vector<std::string>& topics,
               Callback callback);

    /** @brief 幂等停止并等待接收线程退出。 */
    void stop() noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::messaging
