#pragma once
#include <chrono>
using namespace std;
using namespace std::chrono;

class Timer {
   public:
    /** @brief 从当前高精度时钟开始计时。 */
    Timer() : _begin(high_resolution_clock::now()) {}

    /** @brief 将计时起点重置为当前时刻。 */
    void reset() { _begin = high_resolution_clock::now(); }

    /**
     * @brief 返回从计时起点经过的毫秒数。
     * @return 计算得到的数值。
     */
    int64_t elapsed() const {
        return duration_cast<chrono::milliseconds>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的整数秒。
     * @return 计算得到的数值。
     */
    int64_t elapsed_second() const {
        return duration_cast<duration<int64_t>>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的微秒数。
     * @return 计算得到的数值。
     */
    int64_t elapsed_micro() const {
        return duration_cast<chrono::microseconds>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的纳秒数。
     * @return 计算得到的数值。
     */
    int64_t elapsed_nano() const {
        return duration_cast<chrono::nanoseconds>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的秒数。
     * @return 计算得到的数值。
     */
    int64_t elapsed_seconds() const {
        return duration_cast<chrono::seconds>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的分钟数。
     * @return 计算得到的数值。
     */
    int64_t elapsed_minutes() const {
        return duration_cast<chrono::minutes>(high_resolution_clock::now() - _begin).count();
    }

    /**
     * @brief 返回从计时起点经过的小时数。
     * @return 计算得到的数值。
     */
    int64_t elapsed_hours() const {
        return duration_cast<chrono::hours>(high_resolution_clock::now() - _begin).count();
    }

   private:
    time_point<high_resolution_clock> _begin;
};
