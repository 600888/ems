/** @file detail.hpp
 * @brief 托管客户端和服务器共享的线程收尾工具。
 */
#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace dlt698::app::detail {
/** @brief 回调内释放最后句柄时，将当前线程交给受管理的线程回收，不 detach。 */
class JoinQueue {
   public:
    /** @brief 创建唯一的回收工作线程。 */
    JoinQueue() : worker_([this] { run(); }) {}

    /** @brief 排空队列并等待回收线程结束。 */
    ~JoinQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closing_ = true;
        }
        ready_.notify_one();
        worker_.join();
    }

    /** @brief 接收待 join 的线程所有权。
     * @param[in] thread 当前线程不能自行 join 的工作线程。
     */
    void push(std::thread thread) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.push_back(std::move(thread));
        }
        ready_.notify_one();
    }

    /** @brief 获取进程内共享收尾器。
     * @return 静态存活的回收器；启动运行前调用以避免析构路径创建线程。
     */
    static JoinQueue& instance() {
        static JoinQueue queue;
        return queue;
    }

   private:
    /** @brief 逐项回收，关闭且队列为空时结束；join 在队列锁外执行。 */
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
    bool closing_ = false;
    std::thread worker_;
};
}  // namespace dlt698::app::detail
