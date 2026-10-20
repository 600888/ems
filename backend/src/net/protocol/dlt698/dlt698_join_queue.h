/** @file dlt698_join_queue.h
 * @brief 插件专用托管线程回收器，在 DLL 卸载之前显式排空线程。
 */
#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace dlt698::app::detail {
/** @brief 兼容上游回收接口，额外提供可重复启动的显式关闭。 */
class JoinQueue {
   public:
    /** @brief 创建未启动的回收器。 */
    JoinQueue() = default;

    /** @brief 最终清理；正常路径已在 DLL 卸载前停止线程。 */
    ~JoinQueue() { shutdown(); }

    /** @brief 转交不能自行 join 的线程。
     * @param[in] thread 待回收线程，所有权移入队列。
     */
    void push(std::thread thread) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.push_back(std::move(thread));
        }
        ready_.notify_one();
    }

    /** @brief 获取并启动共享回收器；调用方须仍持有协议实例。
     * @return 模块内共享回收器引用。
     */
    static JoinQueue& instance() {
        auto& queue = storage();
        std::lock_guard<std::mutex> lock(queue.mutex_);
        if (!queue.worker_.joinable()) {
            queue.closing_ = false;
            queue.worker_ = std::thread([&queue] { queue.run(); });
        }
        return queue;
    }

    /** @brief 最后一个协议实例释放时显式关闭，不在 DLL loader lock 内执行。
     * @note 须先销毁全部托管客户端和服务器，期间不能启动新协议实例。
     */
    static void shutdown_idle() { storage().shutdown(); }

   private:
    /** @brief 获取不自动启动的存储。
     * @return 模块生命周期内有效的回收器引用。
     */
    static JoinQueue& storage() {
        static JoinQueue queue;
        return queue;
    }

    /** @brief 幂等排空队列并等待回收线程退出。 */
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!worker_.joinable()) return;
            closing_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }

    /** @brief 在队列锁外逐项等待线程，关闭且队列为空时退出。 */
    void run() {
        for (;;) {
            std::thread thread;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return closing_ || !threads_.empty(); });
                if (threads_.empty()) return;
                thread = std::move(threads_.front());
                threads_.pop_front();
            }
            thread.join();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::thread> threads_;
    bool closing_{false};
    std::thread worker_;
};
}  // namespace dlt698::app::detail
