#include <deque>
#include <dlt698/common/executor.hpp>
#include <map>
#include <stdexcept>

namespace dlt698 {
namespace {
struct ManualTimer final : ITimer {
    bool cancelled = false;

    void cancel() override { cancelled = true; }
};

struct Scheduled {
    std::shared_ptr<ManualTimer> timer;
    IExecutor::Task task;
};
}  // namespace

struct ManualExecutor::Impl {
    Clock::time_point time{};
    std::deque<Task> tasks;
    std::multimap<Clock::time_point, Scheduled> timers;
    unsigned running = 0;
};

ManualExecutor::ManualExecutor() : impl_(std::make_unique<Impl>()) {}

ManualExecutor::~ManualExecutor() = default;

void ManualExecutor::post(Task task) { impl_->tasks.push_back(std::move(task)); }

std::shared_ptr<ITimer> ManualExecutor::schedule(Clock::duration delay, Task task) {
    if (delay < Clock::duration::zero() || delay > Clock::time_point::max() - impl_->time)
        throw std::invalid_argument("timer delay");
    auto timer = std::make_shared<ManualTimer>();
    impl_->timers.emplace(impl_->time + delay, Scheduled{timer, std::move(task)});
    return timer;
}

IExecutor::Clock::time_point ManualExecutor::now() const noexcept { return impl_->time; }

bool ManualExecutor::is_current() const noexcept { return impl_->running != 0; }

void ManualExecutor::run_ready() {
    for (;;) {
        // 到期任务也进入 FIFO，避免延时回调在当前回调内部重入。
        for (auto it = impl_->timers.begin(); it != impl_->timers.end();) {
            if (it->second.timer->cancelled) {
                it = impl_->timers.erase(it);
                continue;
            }
            if (it->first <= impl_->time) {
                auto item = std::move(it->second);
                impl_->tasks.push_back([item = std::move(item)] {
                    if (!item.timer->cancelled && item.task) item.task();
                });
                it = impl_->timers.erase(it);
            } else
                ++it;
        }
        if (impl_->tasks.empty()) break;
        auto task = std::move(impl_->tasks.front());
        impl_->tasks.pop_front();
        try {
            ++impl_->running;
            if (task) task();
        } catch (...) { /* 单个用户任务失败不阻断事件循环。 */
        }
        --impl_->running;
    }
}

void ManualExecutor::advance(Clock::duration elapsed) {
    if (elapsed < Clock::duration::zero() || elapsed > Clock::time_point::max() - impl_->time)
        throw std::invalid_argument("clock advance");
    impl_->time += elapsed;
    run_ready();
}
}  // namespace dlt698
