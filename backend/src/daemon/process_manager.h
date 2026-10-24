#pragma once

#include <csignal>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "process.h"

namespace ems::daemon {

/**
 * @brief 子进程守护管理器：加载配置、拉起子进程、监控退出并按策略重启。
 *
 * 除 request_stop() 外，所有成员函数只能在主循环线程调用。进程集合按依赖层级
 * 排序后保存，因此启动顺序确定，停止时反向遍历即可先停依赖方。
 */
class ProcessManager {
   public:
    ProcessManager() = default;
    ~ProcessManager() = default;

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    /**
     * @brief 加载并校验进程管理配置。
     * @param[in] file 配置文件路径。
     * @param[out] error 失败原因。
     * @return 加载成功返回 true。
     */
    bool load_config(const std::string& file, std::string& error);

    /**
     * @brief 校验已加载配置：进程名唯一、命令非空、依赖存在且无环。
     * @param[out] error 失败原因。
     * @return 配置可用返回 true。
     */
    bool validate(std::string& error);

    /**
     * @brief 主循环：回收子进程、按策略启动，收到退出请求后停止全部子进程。
     */
    void run();

    /**
     * @brief 请求退出。可在信号处理函数中调用，只写 sig_atomic_t 标志。
     */
    void request_stop() { stop_requested_ = 1; }

    /**
     * @brief 是否已请求退出。
     * @return 已置停止标志返回 true。
     */
    bool stop_requested() const { return stop_requested_ != 0; }

    /**
     * @brief 按依赖顺序返回托管的进程。
     * @return 进程集合的只读引用。
     */
    const std::vector<std::unique_ptr<Process>>& processes() const { return processes_; }

   private:
    /// @brief 回收所有已退出的子进程并记录退出原因（平台相关，见
    ///        process_posix.cpp / process_win32.cpp）。
    void reap_children();

    /// @brief 记录子进程退出并写日志；两个平台的回收实现共用。
    /// @param[in,out] proc 已退出的进程。
    /// @param[in] pid 退出时的进程号。
    /// @param[in] exit_code 退出码。
    /// @param[in] term_signal 终止信号，0 表示正常退出。
    void finish_exit(Process& proc, ProcessId pid, int exit_code, int term_signal);

    /// @brief 按延迟、间隔、重启次数和依赖关系决定是否拉起进程。
    /// @param[in,out] proc 待检查的进程。
    void maybe_start(Process& proc);

    /// @brief 判断进程依赖的进程是否都在运行。
    /// @param[in] proc 待检查的进程。
    /// @return 依赖全部运行返回 true。
    bool dependencies_running(const Process& proc) const;

    /// @brief 停止全部子进程：先请求优雅退出，超时后强杀并回收。
    void shutdown();

    /// @brief 按 pid 查找托管的进程。
    /// @param[in] pid 子进程号。
    /// @return 找到返回进程指针，否则返回 nullptr。
    Process* find_by_pid(ProcessId pid);

    /// @brief 统计仍在运行的子进程数量。
    /// @return 运行中的进程个数。
    std::size_t running_count() const;

    /// @brief 按依赖层级对进程做稳定排序，并检测循环依赖。
    /// @param[out] error 失败原因。
    /// @return 排序成功返回 true。
    bool sort_by_dependency(std::string& error);

    /// @brief 把托管清单写入日志，便于排查启动顺序和重启策略。
    void log_plan() const;

   private:
    std::vector<std::unique_ptr<Process>> processes_;
    std::unordered_map<std::string, Process*> index_;
    std::string run_path_;
    std::chrono::steady_clock::time_point boot_time_{};
    std::chrono::seconds stop_timeout_{10};
    volatile std::sig_atomic_t stop_requested_ = 0;
};

}  // namespace ems::daemon
