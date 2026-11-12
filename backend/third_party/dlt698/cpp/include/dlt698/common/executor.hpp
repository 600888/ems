/** @file executor.hpp
 * @brief 会话使用的串行执行器及可手动推进的单调时钟。
 */
#pragma once
#include <chrono>
#include <dlt698/export.hpp>
#include <functional>
#include <memory>

namespace dlt698 {
// 必须整类导出：跨动态库派生类需要基类的 typeinfo 和 vtable，
// 只导出成员函数时这两个符号在 ELF/Mach-O 上仍是隐藏的，链接会失败。
class DLT698_API ITimer {
   public:
    /** @brief 释放计时器句柄；释放本身不等同于取消。 */
    virtual ~ITimer() = default;
    /** @brief 幂等取消尚未执行的回调，不产生取消完成通知。 */
    virtual void cancel() = 0;
};

/** @brief 串行执行会话任务的执行器，不得在 post/schedule 内同步执行用户任务。 */
class DLT698_API IExecutor {
   public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    /** @brief 释放执行器接口。 */
    virtual ~IExecutor() = default;
    /** @brief 排队执行任务，同一执行器上的任务不得并发。
     * @param[in] task 待执行任务，异常须由执行器隔离。
     */
    virtual void post(Task task) = 0;
    /** @brief 排队执行延时任务。
     * @param[in] delay 相对于当前单调时间的非负延时。
     * @param[in] task 到期后执行的任务。
     * @return 可用于取消任务的句柄。
     */
    virtual std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) = 0;
    /** @brief 查询单调时间。
     * @return 当前时间点，不受系统日期调整影响。
     */
    virtual Clock::time_point now() const noexcept = 0;
    /** @brief 检查当前线程是否正在执行本执行环境的任务。
     * @return 是时返回 true；共享事件循环的执行器应覆盖整个循环线程，防止同步等待死锁。
     */
    virtual bool is_current() const noexcept = 0;
};

/** @brief 确定性执行器，应用手动驱动；仅限单线程使用，适合内存模拟和虚拟时间测试。 */
class ManualExecutor final : public IExecutor {
   public:
    /** @brief 创建虚拟时钟从零开始的执行器。 */
    DLT698_API ManualExecutor();
    /** @brief 丢弃尚未驱动的任务及计时器。 */
    DLT698_API ~ManualExecutor() override;
    /** @brief 禁止复制事件队列。 */
    ManualExecutor(const ManualExecutor&) = delete;
    /** @brief 禁止复制赋值事件队列。 */
    ManualExecutor& operator=(const ManualExecutor&) = delete;
    /** @brief 投递任务。
     * @param[in] task 待执行任务。
     */
    DLT698_API void post(Task task) override;
    /** @brief 创建虚拟计时器。
     * @param[in] delay 非负延时。
     * @param[in] task 到期任务。
     * @return 取消句柄。
     * @throws std::invalid_argument 延时为负或超出可表示范围。
     */
    DLT698_API std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) override;
    /** @brief 查询虚拟时间。
     * @return 当前虚拟单调时间点。
     */
    DLT698_API Clock::time_point now() const noexcept override;
    /** @brief 检查是否在手动驱动的任务中。
     * @return run_ready/advance 正在执行任务时为 true。
     */
    DLT698_API bool is_current() const noexcept override;
    /** @brief 执行所有当前可运行的任务，包括任务中新投递的任务；隔离回调异常。 */
    DLT698_API void run_ready();
    /** @brief 推进虚拟时间并执行到期任务。
     * @param[in] elapsed 非负推进时长。
     * @throws std::invalid_argument 时间为负或超出可表示范围。
     */
    DLT698_API void advance(Clock::duration elapsed);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace dlt698
