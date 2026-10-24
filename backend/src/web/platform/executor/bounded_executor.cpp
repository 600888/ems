#include "bounded_executor.h"

#include <stdexcept>

namespace ems::web {
BoundedExecutor::BoundedExecutor(std::size_t count, std::size_t capacity) : capacity_(capacity) {
    if (count == 0 || capacity == 0) throw std::invalid_argument("executor bounds");

    for (std::size_t i = 0; i < count; ++i) {
        threads_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                    if (tasks_.empty()) return;

                    // 只在取出任务时持锁，业务处理不能阻塞其他工作线程。
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                    ++active_;
                }
                try {
                    task();
                } catch (...) { /* 单个请求异常不得终止工作线程。 */
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    --active_;
                }
            }
        });
    }
}

BoundedExecutor::~BoundedExecutor() {
    stop();
    wait();
}

void BoundedExecutor::wait() {
    for (auto& thread : threads_)
        if (thread.joinable()) thread.join();
}

bool BoundedExecutor::idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty() && active_ == 0;
}

bool BoundedExecutor::submit(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || tasks_.size() >= capacity_) return false;

    tasks_.push_back(std::move(task));
    changed_.notify_one();
    return true;
}

void BoundedExecutor::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    changed_.notify_all();
}
}  // namespace ems::web
