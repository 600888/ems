#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class Task {
   public:
    /** @brief 构造尚未运行的任务。 */
    Task() : _running(false) {}

    /**
     * @brief 禁止复制任务。
     * @param[in] other 待复制或移动的源对象。
     */
    Task(const Task& other) = delete;

    /**
     * @brief 禁止复制赋值任务。
     * @param[in] other 待复制或移动的源对象。
     */
    Task& operator=(const Task& other) = delete;

    /** @brief 停止任务并等待线程结束。 */
    virtual ~Task() {
        stop();
        // 等待线程结束，防止线程对象被销毁时线程仍在运行
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    /** @brief 启动后台线程执行 svc。 */
    void start() {
        if (!_running) {
            _running = true;
            _thread = std::thread(std::bind(&Task::_start, this));
        }
    }

    /** @brief 请求停止并等待后台线程结束。 */
    virtual void stop() {
        if (_running) {
            _running = false;
            if (_thread.joinable()) {
                _thread.join();
            }
        }
    }

    /** @brief 显式等待可连接的后台线程结束。 */
    void join() { _thread.join(); }

    /** @brief 等待后台线程结束。 */
    void wait() { join(); }

    /**
     * @brief 判断任务是否标记为运行中。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isRunning() const { return _running; }

    /** @brief 阻塞直到派生类通知任务已就绪。 */
    void wait_for_ready() {
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [this] { return _ready; });
    }

   protected:
    /** @brief 在线程入口执行 svc 并清除运行标记。 */
    void _start() {
        svc();
        _running = false;
    }

    /** @brief 由派生类实现任务主体。 */
    virtual void svc() = 0;

   protected:
    std::condition_variable _cv;
    bool _ready = false;
    std::mutex _mtx;

   private:
    std::atomic<bool> _running;
    std::thread _thread;
};
