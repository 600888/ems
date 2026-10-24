/// @file process_win32.cpp
/// @brief 守护进程的 Windows 平台实现。
///
/// CreateProcessW/WaitForSingleObject/TerminateProcess 与命名互斥量单实例锁、
/// 控制台事件退出处理；与 process_posix.cpp 保持同一套接口。

#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "log/default_logger.hpp"
#include "pid_file.h"
#include "platform.h"
#include "process_manager.h"
#include "spdlog/fmt/fmt.h"
#include "util/env.hpp"

namespace ems::daemon {
namespace {

/// @brief 控制台事件在独立线程回调，回调里只允许置标志。
void (*g_quit_callback)() = nullptr;

/// @brief UTF-8 转 UTF-16。
/// @param[in] utf8 输入文本。
/// @return 转换结果；输入为空时返回空串。
std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    if (size > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(),
                              size);
    }
    return wide;
}

/// @brief UTF-16 转 UTF-8。
/// @param[in] wide 以空字符结尾的输入文本。
/// @return 转换结果。
std::string to_utf8(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(size > 0 ? static_cast<std::size_t>(size - 1) : 0, '\0');
    if (size > 1) {
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), size, nullptr, nullptr);
    }
    return utf8;
}

/// @brief 把 GetLastError 转成可读文本。
/// @param[in] what 出错的系统调用名。
/// @return 形如 "CreateProcessW 失败（错误码 2）: 系统找不到指定的文件。" 的文本。
std::string last_error_message(const char* what) {
    const DWORD code = ::GetLastError();
    LPWSTR buffer = nullptr;
    const DWORD size = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer),
        0, nullptr);

    std::string detail;
    if (buffer != nullptr && size > 0) {
        detail = to_utf8(buffer);
        while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n')) {
            detail.pop_back();
        }
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
    return fmt::format("{} 失败（错误码 {}）: {}", what, code,
                       detail.empty() ? "未知错误" : detail);
}

/// @brief 参数含空格、制表符或引号时按 Windows 规则加引号。
/// @param[in] value 单个参数。
/// @return 可直接拼入命令行的文本。
std::string quote_argument(const std::string& value) {
    if (value.empty() || value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
}

/**
 * @brief 组建 CreateProcessW 的命令行：程序名加参数，均做引号处理。
 * @param[in] proc 进程配置。
 * @return UTF-16 命令行；CreateProcessW 可能改写缓冲区，调用方传入可变副本。
 */
std::wstring build_command_line(const Process& proc) {
    std::string line = quote_argument(proc.cmd);
    for (const auto& arg : proc.args) {
        line += ' ';
        line += quote_argument(arg);
    }
    return to_wide(line);
}

/**
 * @brief 组建子进程环境块：继承当前环境并覆盖配置项。
 * @param[in] proc 进程配置。
 * @return "NAME=VALUE\0...\0\0" 形式的 UTF-16 环境块；无覆盖项时返回空串。
 */
std::wstring build_env_block(const Process& proc) {
    if (proc.env.empty()) {
        return {};
    }

    std::unordered_map<std::string, std::string> merged;
    const LPWCH block = ::GetEnvironmentStringsW();
    if (block != nullptr) {
        for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
            if (*entry == L'=') {
                continue;  // 以 '=' 开头的是系统隐藏变量
            }
            const std::string item = to_utf8(entry);
            const auto equals = item.find('=');
            if (equals != std::string::npos) {
                merged.emplace(item.substr(0, equals), item.substr(equals + 1));
            }
        }
        ::FreeEnvironmentStringsW(block);
    }
    for (const auto& [key, value] : proc.env) {
        merged[key] = value;
    }

    std::wstring result;
    for (const auto& [key, value] : merged) {
        result += to_wide(fmt::format("{}={}", key, value));
        result += L'\0';
    }
    result += L'\0';
    return result;
}

}  // namespace

bool Process::spawn(std::string& error) {
    std::wstring command_line = build_command_line(*this);
    std::wstring environment = build_env_block(*this);
    const std::wstring work_dir = run_dir.empty() ? std::wstring() : to_wide(run_dir);

    // 单实例：创建可继承的命名互斥量，子进程继承句柄后由它决定锁生命周期。
    HANDLE child_mutex = nullptr;
    if (single_instance) {
        SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        const std::string mutex_name = fmt::format("Local\\ems-daemon-child-{}", name);
        child_mutex = ::CreateMutexW(&inheritable, FALSE, to_wide(mutex_name).c_str());
        if (child_mutex == nullptr) {
            error = last_error_message("CreateMutexW");
            return false;
        }
        if (::GetLastError() == ERROR_ALREADY_EXISTS) {
            ::CloseHandle(child_mutex);
            ++start_count;
            error = "已有实例在运行，停止托管";
            stop_requested = true;
            return false;
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const BOOL created = ::CreateProcessW(
        nullptr, command_line.data(), nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP,
        environment.empty() ? nullptr : static_cast<LPVOID>(environment.data()),
        work_dir.empty() ? nullptr : work_dir.c_str(), &startup, &info);
    if (child_mutex != nullptr) {
        // 父进程只保留自己的副本：CreateProcess 已把句柄复制进子进程。
        ::CloseHandle(child_mutex);
    }
    if (!created) {
        ++start_count;
        error = last_error_message("CreateProcessW");
        return false;
    }

    ::CloseHandle(info.hThread);
    pid = info.dwProcessId;
    process_handle = info.hProcess;
    status = ProcessStatus::Running;
    ++start_count;
    next_start = std::chrono::steady_clock::now() + start_interval;
    last_start_wall = std::chrono::system_clock::now();
    return true;
}

bool Process::is_running() const {
    if (process_handle == nullptr) {
        return false;
    }
    return ::WaitForSingleObject(process_handle, 0) == WAIT_TIMEOUT;
}

void Process::terminate() const {
    if (process_handle == nullptr) {
        return;
    }
    // CTRL_BREAK 只对以 CREATE_NEW_PROCESS_GROUP 启动的进程组生效；
    // 与目标不在同一控制台时调用失败，退回强杀。
    if (!::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(pid))) {
        (void)::TerminateProcess(process_handle, 1);
    }
}

void Process::kill_force() const {
    if (process_handle != nullptr) {
        (void)::TerminateProcess(process_handle, 1);
    }
}

void ProcessManager::reap_children() {
    for (auto& proc : processes_) {
        if (proc->process_handle == nullptr) {
            continue;
        }
        const HANDLE handle = static_cast<HANDLE>(proc->process_handle);
        if (::WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
            continue;
        }

        DWORD code = 0;
        (void)::GetExitCodeProcess(handle, &code);
        const ProcessId exited = proc->pid;
        finish_exit(*proc, exited, static_cast<int>(code), 0);
        ::CloseHandle(handle);
    }
}

namespace platform {

ProcessId current_process_id() { return ::GetCurrentProcessId(); }

bool acquire_instance_lock(const std::string& name, void*& guard, std::string& error) {
    guard = nullptr;

    const std::string mutex_name = fmt::format("Local\\ems-daemon-instance-{}", name);
    const HANDLE mutex = ::CreateMutexW(nullptr, FALSE, to_wide(mutex_name).c_str());
    if (mutex == nullptr) {
        error = last_error_message("CreateMutexW");
        return false;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(mutex);
        error = "已有实例持有实例锁";
        return false;
    }

    // pid 文件仅用于诊断展示，写入失败不影响互斥。
    std::error_code fs_code;
    const std::filesystem::path pid_path =
        std::filesystem::path(runPath()) / fmt::format("{}.pid", name);
    std::ofstream output(pid_path, std::ios::trunc);
    if (output.is_open()) {
        output << ::GetCurrentProcessId();
    }

    guard = mutex;
    return true;
}

void release_instance_lock(void* guard) {
    if (guard != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(guard));
    }
}

bool daemonize(std::string& error) {
    // Windows 没有与 POSIX 对应的后台化语义，保持前台运行。
    (void)error;
    return true;
}

bool install_quit_handler(void (*on_quit)(), std::string& error) {
    g_quit_callback = on_quit;
    if (!::SetConsoleCtrlHandler(
            [](DWORD event) -> BOOL {
                switch (event) {
                    case CTRL_C_EVENT:
                    case CTRL_BREAK_EVENT:
                    case CTRL_CLOSE_EVENT:
                    case CTRL_LOGOFF_EVENT:
                    case CTRL_SHUTDOWN_EVENT:
                        if (g_quit_callback != nullptr) {
                            g_quit_callback();
                        }
                        return TRUE;
                    default:
                        return FALSE;
                }
            },
            TRUE)) {
        error = last_error_message("SetConsoleCtrlHandler");
        return false;
    }
    return true;
}

}  // namespace platform
}  // namespace ems::daemon
