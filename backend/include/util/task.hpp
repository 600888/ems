#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class Task {
   public:
    Task() : _running(false) {}
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    virtual ~Task() {
        stop();
        // 等待线程结束，防止线程对象被销毁时线程仍在运行
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    void start() {
        if (!_running) {
            _running = true;
            _thread = std::thread(std::bind(&Task::_start, this));
        }
    }

    virtual void stop() {
        if (_running) {
            _running = false;
            if (_thread.joinable()) {
                _thread.join();
            }
        }
    }

    // 显式等待线程结束，防止主线程提前退出导致子线程退出
    void join() { _thread.join(); }

    void wait() { join(); }

    bool isRunning() const { return _running; }

    void wait_for_ready() {
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [this] { return _ready; });
    }

   protected:
    void _start() {
        svc();
        _running = false;
    }

    virtual void svc() = 0;

   protected:
    std::condition_variable _cv;
    bool _ready = false;
    std::mutex _mtx;

   private:
    std::atomic<bool> _running;
    std::thread _thread;
};