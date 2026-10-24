#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ems::web {
class BoundedExecutor {
   public:
    /**
     * @brief 创建固定数量工作线程。
     * @param[in] threads 工作线程数。
     * @param[in] capacity 排队上限。
     */
    BoundedExecutor(std::size_t threads, std::size_t capacity);
    /** @brief 停止准入，完成在途任务并等待工作线程退出。 */
    ~BoundedExecutor();
    /**
     * @brief 非阻塞提交任务。
     * @param[in] task 自有请求状态和截止时间的任务。
     * @return false 表示停止或队列已满；调用方立即回复过载。
     */
    bool submit(std::function<void()> task);
    /** @brief 停止新准入；排队任务按自身期限完成或回复过期。 */
    void stop();
    /** @brief 检查排队和在途任务是否全部结束。
     * @return 已无待执行或正在执行任务时为 true。
     */
    bool idle();
    /** @brief 等待已停止的执行器退出；只由进程装配线程调用。 */
    void wait();

   private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
    std::vector<std::thread> threads_;
    std::size_t capacity_;
    bool stopping_{false};
    std::size_t active_{0};
};
}  // namespace ems::web
