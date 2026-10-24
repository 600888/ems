#include <iostream>
#include <string>

#include "daemon_log.h"
#include "log/default_logger.hpp"
#include "pid_file.h"
#include "platform.h"
#include "process_manager.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"
#include "util/conf.hpp"
#include "util/env.hpp"

namespace {

/// 全局管理器：退出回调需要稳定的对象地址。
ems::daemon::ProcessManager g_manager;

/// @brief 退出回调：只置停止标志，不做日志、内存分配和子进程回收。
void handle_stop_signal() { g_manager.request_stop(); }

void print_usage() {
    std::cout << "Usage: ems-daemon [options]\n"
                 "  -f, --foreground     前台运行（默认）\n"
                 "  -d, --daemon         后台运行（Linux 下脱离终端；Windows 保持前台）\n"
                 "  -c, --config <file>  配置文件，默认 <EMS_ROOT>/conf/proc_manage.json\n"
                 "      --check-config   仅校验配置后退出\n"
                 "  -h, --help           显示帮助\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_file = emsdConfPath();
    bool foreground = true;
    bool check_only = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" || argument == "-c") {
            if (index + 1 >= argc) {
                std::cerr << "参数 " << argument << " 需要文件路径\n";
                return 2;
            }
            config_file = argv[++index];
        } else if (argument == "--daemon" || argument == "-d") {
            foreground = false;
        } else if (argument == "--foreground" || argument == "-f") {
            foreground = true;
        } else if (argument == "--check-config") {
            check_only = true;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "未知参数: " << argument << '\n';
            print_usage();
            return 2;
        }
    }

    std::string error;
    if (!foreground && !ems::daemon::platform::daemonize(error)) {
        std::cerr << "转为后台进程失败: " << error << '\n';
        return 1;
    }

    ems::daemon::init_daemon_logger(foreground);

    if (!g_manager.load_config(config_file, error)) {
        LOG_ERROR("加载进程管理配置失败: {}", error);
        std::cerr << "加载进程管理配置失败: " << error << '\n';
        return 1;
    }

    if (check_only) {
        LOG_INFO("配置校验通过，共 {} 个进程", g_manager.processes().size());
        std::cout << "配置校验通过: " << config_file << "，共 " << g_manager.processes().size()
                  << " 个进程\n";
        return 0;
    }

    const std::string run_dir = runPath();
    if (!ems::daemon::ensure_pid_directory(run_dir, error)) {
        LOG_ERROR("创建运行目录 {} 失败: {}", run_dir, error);
        return 1;
    }

    // 单实例锁：POSIX 为 pid 文件锁，Windows 为命名互斥量；必须持有到进程退出。
    void* instance_guard = nullptr;
    if (!ems::daemon::platform::acquire_instance_lock("ems-daemon", instance_guard, error)) {
        LOG_ERROR("ems-daemon 已在运行或获取实例锁失败: {}", error);
        std::cerr << "ems-daemon 已在运行或获取实例锁失败: " << error << '\n';
        return 1;
    }

    if (!ems::daemon::platform::install_quit_handler(&handle_stop_signal, error)) {
        LOG_ERROR("注册退出处理失败: {}", error);
        ems::daemon::platform::release_instance_lock(instance_guard);
        return 1;
    }

    LOG_INFO("ems-daemon 启动成功, pid={}, 配置文件={}",
             ems::daemon::platform::current_process_id(), config_file);
    g_manager.run();

    ems::daemon::platform::release_instance_lock(instance_guard);
    LOG_INFO("ems-daemon 退出");
    // 等待异步日志线程把剩余日志刷盘后再返回
    spdlog::shutdown();
    return 0;
}
