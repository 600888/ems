/// @file process_posix.cpp
/// @brief 守护进程的 POSIX 平台实现（Linux 目标）。
///
/// fork/exec/waitpid/pid 文件锁与信号处理；与 process_win32.cpp 保持同一套接口。

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "log/default_logger.hpp"
#include "pid_file.h"
#include "platform.h"
#include "process_manager.h"
#include "spdlog/fmt/fmt.h"
#include "util/env.hpp"

namespace ems::daemon {
namespace {

/// @brief 退出回调在信号处理函数中执行，只允许置标志。
void (*g_quit_callback)() = nullptr;

/// @brief 子进程 exec 前失败时通过管道回传 errno；写失败无需处理。
void report_child_error(int fd, int code) {
    const ssize_t ignored = ::write(fd, &code, sizeof(code));
    (void)ignored;
}

/**
 * @brief 在父进程内组装 argv。
 *
 * fork 之后子进程不能再做内存分配，因此 argv 必须在 fork 前准备好。
 * @param[in] proc 进程配置。
 * @param[in] command 解析后的可执行文件路径。
 * @return 以 nullptr 结尾的参数数组，指向 proc 内部的字符串。
 */
std::vector<char*> build_argv(const Process& proc, const std::string& command) {
    std::vector<char*> argv;
    argv.reserve(proc.args.size() + 2);
    argv.push_back(const_cast<char*>(command.c_str()));
    for (const auto& arg : proc.args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

/**
 * @brief 在父进程内组装环境变量块：继承当前环境并覆盖配置项。
 * @param[in] proc 进程配置。
 * @return "key=value" 字符串列表。
 */
std::vector<std::string> build_env_storage(const Process& proc) {
    std::vector<std::string> storage;
    if (proc.env.empty()) {
        return storage;
    }

    for (char** item = environ; item != nullptr && *item != nullptr; ++item) {
        const char* equals = std::strchr(*item, '=');
        const std::string key =
            equals == nullptr ? std::string(*item) : std::string(*item, equals - *item);
        if (proc.env.find(key) == proc.env.end()) {
            storage.emplace_back(*item);
        }
    }

    for (const auto& [key, value] : proc.env) {
        storage.push_back(fmt::format("{}={}", key, value));
    }
    return storage;
}

/**
 * @brief 解析命令：不含路径分隔符时按 PATH 查找可执行文件。
 * @param[in] cmd 配置的命令。
 * @return 找到返回绝对/相对路径；找不到时原样返回，交给 execve 报 ENOENT。
 */
std::string resolve_command(const std::string& cmd) {
    if (cmd.find('/') != std::string::npos) {
        return cmd;
    }

    const char* path_env = ::getenv("PATH");
    if (path_env == nullptr) {
        return cmd;
    }

    const std::string_view remain(path_env);
    std::string_view::size_type begin = 0;
    while (begin <= remain.size()) {
        const auto end = remain.find(':', begin);
        const std::string_view dir = remain.substr(
            begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        if (!dir.empty()) {
            const std::string candidate = fmt::format("{}/{}", dir, cmd);
            if (::access(candidate.c_str(), X_OK) == 0) {
                return candidate;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return cmd;
}

/**
 * @brief 子进程内对 pid 文件加锁并写入进程号。
 *
 * open/F_SETLK/ftruncate/write 都不涉及内存分配，可以在 fork 之后调用；
 * 故意不关闭描述符：锁随 exec 保留，与子进程同生命周期。
 * @param[in] path pid 文件路径（父进程已生成）。
 * @param[in] report_fd 失败时回传 errno 的管道写端。
 * @return 成功返回持有的 fd，失败返回 -1。
 */
int lock_child_pid_file(const std::string& path, int report_fd) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        report_child_error(report_fd, errno);
        return -1;
    }

    struct flock file_lock{};
    file_lock.l_type = F_WRLCK;
    file_lock.l_whence = SEEK_SET;
    if (::fcntl(fd, F_SETLK, &file_lock) < 0) {
        const int code = (errno == EACCES || errno == EAGAIN) ? EEXIST : errno;
        report_child_error(report_fd, code);
        ::close(fd);
        return -1;
    }

    char buffer[32] = {};
    const auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), static_cast<long long>(::getpid()));
    if (converted.ec == std::errc()) {
        (void)::ftruncate(fd, 0);
        (void)::write(fd, buffer, static_cast<std::size_t>(converted.ptr - buffer));
    }
    return fd;
}

}  // namespace

bool Process::spawn(std::string& error) {
    const std::string command = resolve_command(cmd);
    const std::vector<char*> argv = build_argv(*this, command);
    std::vector<std::string> env_storage = build_env_storage(*this);
    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (auto& item : env_storage) {
        envp.push_back(item.data());
    }
    envp.push_back(nullptr);

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        error = std::strerror(errno);
        return false;
    }

    // 写端设置 FD_CLOEXEC：exec 成功时自动关闭，父进程读到 EOF 即代表启动成功。
    if (::fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        error = std::strerror(errno);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return false;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        error = std::strerror(errno);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return false;
    }

    if (child == 0) {
        ::close(pipe_fds[0]);

        // 子进程只允许调用异步信号安全接口：fork 之后不能再用锁、内存分配和日志线程。
        (void)::setsid();

        if (!run_dir.empty() && ::chdir(run_dir.c_str()) != 0) {
            report_child_error(pipe_fds[1], errno);
            ::_exit(EXIT_FAILURE);
        }

        if (single_instance && lock_child_pid_file(pid_file, pipe_fds[1]) < 0) {
            // 加锁失败（含已有实例）：错误码已经通过管道回传给父进程
            ::_exit(EXIT_SUCCESS);
        }

        ::execvpe(command.c_str(), argv.data(), env_storage.empty() ? environ : envp.data());
        report_child_error(pipe_fds[1], errno);
        ::_exit(127);
    }

    ::close(pipe_fds[1]);

    int child_error = 0;
    ssize_t read_size = 0;
    do {
        read_size = ::read(pipe_fds[0], &child_error, sizeof(child_error));
    } while (read_size < 0 && errno == EINTR);
    ::close(pipe_fds[0]);

    if (read_size == static_cast<ssize_t>(sizeof(child_error))) {
        // 子进程未 exec 就退出，先回收避免留下僵尸进程。
        int wait_status = 0;
        while (::waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {
        }

        ++start_count;
        if (child_error == EEXIST) {
            error = "已有实例在运行，停止托管";
            stop_requested = true;
        } else {
            error = std::strerror(child_error);
        }
        return false;
    }

    pid = child;
    status = ProcessStatus::Running;
    ++start_count;
    next_start = std::chrono::steady_clock::now() + start_interval;
    last_start_wall = std::chrono::system_clock::now();
    return true;
}

bool Process::is_running() const {
    if (pid <= 0) {
        return false;
    }
    if (::kill(pid, 0) == 0) {
        return true;
    }
    // 无权限说明进程存在，只是不属于当前用户。
    return errno == EPERM;
}

void Process::terminate() const {
    if (pid > 0) {
        (void)::kill(pid, SIGTERM);
    }
}

void Process::kill_force() const {
    if (pid > 0) {
        (void)::kill(pid, SIGKILL);
    }
}

void ProcessManager::reap_children() {
    int wait_status = 0;
    while (true) {
        const pid_t exited = ::waitpid(-1, &wait_status, WNOHANG);
        if (exited <= 0) {
            // 0 表示暂无子进程退出；-1 表示没有可回收的子进程
            break;
        }

        Process* proc = find_by_pid(exited);
        if (proc == nullptr) {
            LOG_DEBUG("回收未托管的子进程 pid={}", exited);
            continue;
        }

        int exit_code = -1;
        int term_signal = 0;
        if (WIFEXITED(wait_status)) {
            exit_code = WEXITSTATUS(wait_status);
        } else if (WIFSIGNALED(wait_status)) {
            term_signal = WTERMSIG(wait_status);
        }
        finish_exit(*proc, exited, exit_code, term_signal);
    }
}

namespace platform {

ProcessId current_process_id() { return ::getpid(); }

bool acquire_instance_lock(const std::string& name, void*& guard, std::string& error) {
    guard = nullptr;

    const std::string run_dir = runPath();
    if (!ensure_pid_directory(run_dir, error)) {
        error = fmt::format("创建运行目录失败: {}", error);
        return false;
    }

    const std::string path = fmt::format("{}{}.pid", run_dir, name);
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) {
        error = std::strerror(errno);
        return false;
    }

    struct flock file_lock{};
    file_lock.l_type = F_WRLCK;
    file_lock.l_whence = SEEK_SET;
    if (::fcntl(fd, F_SETLK, &file_lock) < 0) {
        error =
            (errno == EACCES || errno == EAGAIN) ? "已有实例持有 pid 文件锁" : std::strerror(errno);
        ::close(fd);
        return false;
    }

    const std::string text = std::to_string(static_cast<long long>(::getpid()));
    if (::ftruncate(fd, 0) != 0 ||
        ::write(fd, text.data(), text.size()) != static_cast<ssize_t>(text.size())) {
        error = std::strerror(errno);
        ::close(fd);
        return false;
    }

    guard = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    return true;
}

void release_instance_lock(void* guard) {
    if (guard != nullptr) {
        ::close(static_cast<int>(reinterpret_cast<intptr_t>(guard)));
    }
}

bool daemonize(std::string& error) {
    pid_t pid = ::fork();
    if (pid < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (pid > 0) {
        ::_exit(EXIT_SUCCESS);
    }

    if (::setsid() < 0) {
        error = std::strerror(errno);
        return false;
    }

    // 第二次 fork：保证后台进程不是会话首进程，无法重新获得控制终端。
    pid = ::fork();
    if (pid < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (pid > 0) {
        ::_exit(EXIT_SUCCESS);
    }

    (void)::umask(022);
    // 工作目录切到 EMS_ROOT；子进程未配置 runDir 时继承该目录。
    (void)::chdir(rootPath().c_str());

    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        error = std::strerror(errno);
        return false;
    }
    for (const int target : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (::dup2(null_fd, target) < 0) {
            error = std::strerror(errno);
            ::close(null_fd);
            return false;
        }
    }
    if (null_fd > STDERR_FILENO) {
        ::close(null_fd);
    }
    return true;
}

bool install_quit_handler(void (*on_quit)(), std::string& error) {
    g_quit_callback = on_quit;

    struct sigaction action{};
    action.sa_handler = [](int) {
        if (g_quit_callback != nullptr) {
            g_quit_callback();
        }
    };
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;

    for (const int signal_number : {SIGINT, SIGTERM, SIGQUIT}) {
        if (::sigaction(signal_number, &action, nullptr) != 0) {
            error = std::strerror(errno);
            return false;
        }
    }

    // 子进程退出由主循环 waitpid 回收；忽略 SIGPIPE，避免写已关闭管道时被终止。
    (void)::signal(SIGPIPE, SIG_IGN);
    return true;
}

}  // namespace platform
}  // namespace ems::daemon
