#pragma once

#ifndef _WIN32
#include <sys/types.h>
#endif

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace ems::daemon {

#ifdef _WIN32
/// @brief 进程号类型；Windows 为 DWORD，POSIX 为 pid_t，0 表示没有在运行的进程。
using ProcessId = unsigned long;
#else
/// @brief 进程号类型；Windows 为 DWORD，POSIX 为 pid_t，0 表示没有在运行的进程。
using ProcessId = pid_t;
#endif

/// @brief 子进程运行状态。
enum class ProcessStatus {
    /// 未运行（已退出、启动失败或尚未启动）
    Stopped,
    /// 已拉起并等待退出
    Running,
};

/**
 * @brief 守护进程托管的一个子进程：配置项与运行时状态。
 *
 * 配置字段在加载配置文件时写入；运行时字段只在守护进程主循环中更新，
 * 因此结构本身不需要加锁。信号/控制台事件处理函数不触碰该结构。
 */
struct Process {
    // ---- 配置项 ----
    /// 进程名，同时作为日志与单实例锁标识，必须唯一
    std::string name;
    /// 可执行文件；POSIX 相对名走 PATH 查找，Windows 由 CreateProcess 搜索
    std::string cmd;
    /// 命令行参数（不含 argv[0]）
    std::vector<std::string> args;
    /// 子进程工作目录，空表示继承守护进程的目录
    std::string run_dir;
    /// 依赖的进程名，依赖未运行时本进程不启动
    std::vector<std::string> depends;
    /// 追加或覆盖的环境变量
    std::unordered_map<std::string, std::string> env;
    /// 是否通过单实例锁保证只有一个实例
    bool single_instance = true;
    /// 是否无限重启（由 start.cnt 为负数推导）
    bool restart_unlimited = false;
    /// 启动次数上限（含首次启动），负数表示不限制
    int max_start_count = 1;
    /// 两次启动之间的最小间隔
    std::chrono::seconds start_interval{5};
    /// 首次启动的延迟时间
    std::chrono::seconds start_delay{0};

    // ---- 运行时状态 ----
    /// 当前子进程号，0 表示没有在运行的子进程
    ProcessId pid = 0;
    /// 子进程句柄（Windows 为 HANDLE，用于等待与终止；POSIX 恒为 nullptr）
    void* process_handle = nullptr;
    /// 运行状态
    ProcessStatus status = ProcessStatus::Stopped;
    /// 已尝试的启动次数（失败的启动也计入）
    int start_count = 0;
    /// 最近一次退出码；被信号终止时为 -1
    int exit_code = 0;
    /// 最近一次终止该进程的信号，0 表示正常退出
    int term_signal = 0;
    /// 守护进程主动要求停止，不应再重启
    bool stop_requested = false;
    /// 启动次数用尽，不再重启（仅用于避免重复告警）
    bool exhausted = false;
    /// 下次允许启动的最早时刻
    std::chrono::steady_clock::time_point next_start{};
    /// 最近一次启动的墙上时间，仅用于日志
    std::chrono::system_clock::time_point last_start_wall{};
    /// 单实例锁文件路径（Windows 仅作诊断展示，锁本体是命名互斥量）
    std::string pid_file;

    /**
     * @brief 拉起子进程（平台相关实现见 process_posix.cpp / process_win32.cpp）。
     *
     * POSIX：fork + execve，父进程通过 CLOEXEC 管道确认 exec 是否成功；
     * Windows：CreateProcessW，单实例用可继承命名互斥量保证。
     * 两条路径都保证：子进程内 exec 前只使用异步信号安全接口。
     * @param[out] error 失败原因；单实例被占用时通过 stop_requested 标记。
     * @return 子进程成功启动返回 true。
     */
    bool spawn(std::string& error);

    /**
     * @brief 判断记录的子进程是否仍在运行。
     * @return 存活返回 true（POSIX 无权限视为运行中）。
     */
    bool is_running() const;

    /// @brief 请求子进程优雅退出：POSIX 发 SIGTERM；Windows 发 CTRL_BREAK，失败则强杀。
    void terminate() const;

    /// @brief 强制结束子进程：POSIX 发 SIGKILL；Windows 调 TerminateProcess。
    void kill_force() const;

    /**
     * @brief 记录退出信息，并把状态复位为 Stopped。
     * @param[in] exit_code 退出码；被信号终止时为 -1。
     * @param[in] term_signal 终止信号，0 表示正常退出。
     */
    void record_exit(int exit_code, int term_signal);

    /**
     * @brief 生成用于日志的进程描述。
     * @return 单行描述文本。
     */
    std::string description() const;
};

}  // namespace ems::daemon
