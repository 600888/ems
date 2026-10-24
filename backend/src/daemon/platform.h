#pragma once

#include <string>

#include "process.h"

namespace ems::daemon::platform {

/**
 * @brief 返回当前进程号。
 * @return 进程号。
 */
ProcessId current_process_id();

/**
 * @brief 获取守护进程单实例锁。
 *
 * POSIX 对 <EMS_ROOT>/run/<name>.pid 加文件锁；Windows 用命名互斥量
 * Local\ems-daemon-instance-<name>，并尽力写入 pid 文件供诊断。
 * 锁随进程退出自动释放，调用方必须持有 guard 直到退出（提前释放会立刻失去互斥）。
 * @param[in] name 实例名。
 * @param[out] guard 锁句柄。
 * @param[out] error 失败原因。
 * @return 获取成功返回 true。
 */
bool acquire_instance_lock(const std::string& name, void*& guard, std::string& error);

/**
 * @brief 释放单实例锁；进程退出时系统也会自动释放。
 * @param[in] guard acquire_instance_lock 返回的句柄。
 */
void release_instance_lock(void* guard);

/**
 * @brief 转为后台进程。POSIX 两次 fork 并把标准流重定向到 /dev/null；
 *        Windows 没有对应语义，直接成功（保持前台运行）。
 * @param[out] error 失败原因。
 * @return 调用进程应继续运行时返回 true，POSIX 的中间父进程不返回。
 */
bool daemonize(std::string& error);

/**
 * @brief 安装退出处理：POSIX 为 SIGINT/SIGTERM/SIGQUIT；Windows 为控制台事件
 *        （Ctrl+C、关闭窗口、注销、关机）。
 * @param[in] on_quit 收到退出请求时的回调，只允许置标志，不做清理动作。
 * @param[out] error 失败原因。
 * @return 安装成功返回 true。
 */
bool install_quit_handler(void (*on_quit)(), std::string& error);

}  // namespace ems::daemon::platform
