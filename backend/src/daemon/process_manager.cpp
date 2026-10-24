#include "process_manager.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "log/default_logger.hpp"
#include "pid_file.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/fmt/ranges.h"
#include "util/env.hpp"

namespace ems::daemon {
namespace {

using Json = nlohmann::json;

/// @brief 生成配置项位置描述，便于定位错误。
/// @param[in] position processes 数组下标。
/// @return 形如 processes[2] 的文本。
std::string position_name(std::size_t position) { return fmt::format("processes[{}]", position); }

/**
 * @brief 解析单个进程配置项。
 * @param[in] node 进程配置 JSON 对象。
 * @param[in] position 在 processes 数组中的下标，仅用于错误提示。
 * @param[out] proc 解析结果，只填写配置字段。
 * @param[out] error 失败原因。
 * @return 解析成功返回 true。
 */
bool parse_process(const Json& node, std::size_t position, Process& proc, std::string& error) {
    const std::string where = position_name(position);
    if (!node.is_object()) {
        error = fmt::format("{} 必须是对象", where);
        return false;
    }

    auto read_string = [&](const char* key, std::string& out) -> bool {
        if (!node.contains(key)) {
            return true;
        }
        if (!node[key].is_string()) {
            error = fmt::format("{} 的 {} 必须是字符串", where, key);
            return false;
        }
        out = node[key].get<std::string>();
        return true;
    };

    auto read_bool = [&](const char* key, bool& out) -> bool {
        if (!node.contains(key)) {
            return true;
        }
        if (!node[key].is_boolean()) {
            error = fmt::format("{} 的 {} 必须是布尔值", where, key);
            return false;
        }
        out = node[key].get<bool>();
        return true;
    };

    auto read_seconds = [&](const Json& object, const char* key,
                            std::chrono::seconds& out) -> bool {
        if (!object.contains(key)) {
            return true;
        }
        if (!object[key].is_number_integer()) {
            error = fmt::format("{} 的 start.{} 必须是整数秒", where, key);
            return false;
        }
        const int value = object[key].get<int>();
        if (value < 0) {
            error = fmt::format("{} 的 start.{} 不能为负数", where, key);
            return false;
        }
        out = std::chrono::seconds(value);
        return true;
    };

    if (!node.contains("name") || !node["name"].is_string()) {
        error = fmt::format("{} 缺少 name", where);
        return false;
    }
    proc.name = node["name"].get<std::string>();

    if (!node.contains("cmd") || !node["cmd"].is_string()) {
        error = fmt::format("{} 缺少 cmd", where);
        return false;
    }
    proc.cmd = node["cmd"].get<std::string>();

    if (node.contains("args")) {
        if (!node["args"].is_array()) {
            error = fmt::format("{} 的 args 必须是数组", where);
            return false;
        }
        for (const auto& item : node["args"]) {
            if (!item.is_string()) {
                error = fmt::format("{} 的 args 元素必须是字符串", where);
                return false;
            }
            proc.args.push_back(item.get<std::string>());
        }
    }

    if (!read_string("runDir", proc.run_dir)) {
        return false;
    }
    if (!read_bool("singleInstance", proc.single_instance)) {
        return false;
    }

    if (node.contains("depends")) {
        if (!node["depends"].is_array()) {
            error = fmt::format("{} 的 depends 必须是数组", where);
            return false;
        }
        for (const auto& item : node["depends"]) {
            if (!item.is_string()) {
                error = fmt::format("{} 的 depends 元素必须是字符串", where);
                return false;
            }
            proc.depends.push_back(item.get<std::string>());
        }
    }

    if (node.contains("env")) {
        const auto& env = node["env"];
        if (env.is_object()) {
            for (auto iter = env.begin(); iter != env.end(); ++iter) {
                if (!iter.value().is_string()) {
                    error = fmt::format("{} 的 env.{} 必须是字符串", where, iter.key());
                    return false;
                }
                proc.env[iter.key()] = iter.value().get<std::string>();
            }
        } else if (env.is_array()) {
            // 兼容参考实现的 [{"name": "...", "value": "..."}] 写法
            for (const auto& item : env) {
                if (!item.is_object() || !item.contains("name") || !item["name"].is_string()) {
                    error = fmt::format("{} 的 env 数组元素必须包含 name", where);
                    return false;
                }
                const std::string key = item["name"].get<std::string>();
                if (!item.contains("value")) {
                    proc.env[key] = "";
                } else {
                    proc.env[key] = item["value"].is_string() ? item["value"].get<std::string>()
                                                              : item["value"].dump();
                }
            }
        } else {
            error = fmt::format("{} 的 env 必须是对象或数组", where);
            return false;
        }
    }

    if (node.contains("start")) {
        const auto& start = node["start"];
        if (!start.is_object()) {
            error = fmt::format("{} 的 start 必须是对象", where);
            return false;
        }
        if (start.contains("cnt")) {
            if (!start["cnt"].is_number_integer()) {
                error = fmt::format("{} 的 start.cnt 必须是整数", where);
                return false;
            }
            const int count = start["cnt"].get<int>();
            if (count < 0) {
                proc.restart_unlimited = true;
                proc.max_start_count = -1;
            } else {
                proc.max_start_count = count;
            }
        }
        if (!read_seconds(start, "interval", proc.start_interval)) {
            return false;
        }
        if (!read_seconds(start, "delay", proc.start_delay)) {
            return false;
        }
    }

    return true;
}

}  // namespace

bool ProcessManager::load_config(const std::string& file, std::string& error) {
    processes_.clear();
    index_.clear();
    run_path_ = runPath();
    boot_time_ = std::chrono::steady_clock::now();

    if (!ensure_pid_directory(run_path_, error)) {
        error = fmt::format("创建运行目录失败: {}", error);
        return false;
    }

    std::ifstream input(file);
    if (!input.is_open()) {
        error = fmt::format("无法打开配置文件 {}", file);
        return false;
    }

    Json document;
    try {
        input >> document;
    } catch (const std::exception& exception) {
        error = fmt::format("解析配置文件失败: {}", exception.what());
        return false;
    }

    if (!document.is_object()) {
        error = "配置文件根节点必须是对象";
        return false;
    }

    if (document.contains("stopTimeout")) {
        if (!document["stopTimeout"].is_number_integer()) {
            error = "stopTimeout 必须是整数秒";
            return false;
        }
        const int seconds = document["stopTimeout"].get<int>();
        if (seconds < 0) {
            error = "stopTimeout 不能为负数";
            return false;
        }
        stop_timeout_ = std::chrono::seconds(seconds);
    }

    if (!document.contains("processes") || !document["processes"].is_array()) {
        error = "配置缺少 processes 数组";
        return false;
    }

    std::size_t position = 0;
    for (const auto& node : document["processes"]) {
        auto proc = std::make_unique<Process>();
        if (!parse_process(node, position, *proc, error)) {
            return false;
        }
        if (index_.find(proc->name) != index_.end()) {
            error = fmt::format("进程名重复: {}", proc->name);
            return false;
        }

        // 相对工作目录按 EMS_ROOT 解析，绝对路径保持不变。
        if (!proc->run_dir.empty() && std::filesystem::path(proc->run_dir).is_relative()) {
            proc->run_dir = (std::filesystem::path(rootPath()) / proc->run_dir).string();
        }
        proc->pid_file = fmt::format("{}{}.pid", run_path_, proc->name);
        proc->next_start = boot_time_ + proc->start_delay;

        index_[proc->name] = proc.get();
        processes_.push_back(std::move(proc));
        ++position;
    }

    if (!validate(error)) {
        return false;
    }
    if (!sort_by_dependency(error)) {
        return false;
    }

    log_plan();
    return true;
}

bool ProcessManager::validate(std::string& error) {
    if (processes_.empty()) {
        error = "配置中没有任何进程";
        return false;
    }

    for (const auto& proc : processes_) {
        if (proc->name.empty()) {
            error = "进程名不能为空";
            return false;
        }
        if (proc->cmd.empty()) {
            error = fmt::format("进程 {} 的 cmd 不能为空", proc->name);
            return false;
        }
        for (const auto& depend : proc->depends) {
            if (depend == proc->name) {
                error = fmt::format("进程 {} 不能依赖自身", proc->name);
                return false;
            }
            if (index_.find(depend) == index_.end()) {
                error = fmt::format("进程 {} 依赖的 {} 未定义", proc->name, depend);
                return false;
            }
        }
        if (!proc->restart_unlimited && proc->max_start_count == 0) {
            LOG_INFO("进程 {} 的 start.cnt 为 0，将保持禁用状态", proc->name);
        }
    }
    return true;
}

bool ProcessManager::sort_by_dependency(std::string& error) {
    std::unordered_map<std::string, int> depth;
    // 0 未访问，1 访问中（用于发现环），2 已完成
    std::unordered_map<std::string, int> state;

    std::function<bool(const std::string&)> visit = [&](const std::string& name) -> bool {
        const auto iter = index_.find(name);
        if (iter == index_.end()) {
            error = fmt::format("依赖的进程 {} 未定义", name);
            return false;
        }

        int& current = state[name];
        if (current == 2) {
            return true;
        }
        if (current == 1) {
            error = fmt::format("进程依赖存在环: {}", name);
            return false;
        }
        current = 1;

        int level = 0;
        for (const auto& depend : iter->second->depends) {
            if (!visit(depend)) {
                return false;
            }
            level = std::max(level, depth[depend] + 1);
        }

        depth[name] = level;
        current = 2;
        return true;
    };

    for (const auto& proc : processes_) {
        if (!visit(proc->name)) {
            return false;
        }
    }

    std::stable_sort(
        processes_.begin(), processes_.end(),
        [&depth](const std::unique_ptr<Process>& left, const std::unique_ptr<Process>& right) {
            return depth[left->name] < depth[right->name];
        });
    return true;
}

void ProcessManager::log_plan() const {
    LOG_INFO("共托管 {} 个进程，停止超时 {} 秒", processes_.size(), stop_timeout_.count());
    for (const auto& proc : processes_) {
        LOG_INFO("托管进程 {}: cmd={}, 依赖[{}], 启动上限={}, 延迟={}s, 间隔={}s, 单实例={}",
                 proc->name, proc->cmd,
                 proc->depends.empty() ? "-" : fmt::format("{}", fmt::join(proc->depends, ",")),
                 proc->restart_unlimited ? -1 : proc->max_start_count, proc->start_delay.count(),
                 proc->start_interval.count(), proc->single_instance ? "是" : "否");
    }
}

void ProcessManager::run() {
    LOG_INFO("ems-daemon 进入主循环");
    while (!stop_requested()) {
        reap_children();
        for (auto& proc : processes_) {
            maybe_start(*proc);
        }
        // 100ms 轮询周期：兼顾退出响应速度与 CPU 占用
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("收到退出请求，开始停止全部子进程");
    shutdown();
    LOG_INFO("全部子进程已停止");
}

void ProcessManager::finish_exit(Process& proc, ProcessId pid, int exit_code, int term_signal) {
    proc.record_exit(exit_code, term_signal);
    if (proc.stop_requested) {
        LOG_INFO("进程 {} 已按要求退出", proc.name);
    } else if (term_signal != 0) {
        LOG_WARN("进程 {}(pid={}) 被信号 {} 终止", proc.name, pid, term_signal);
    } else {
        LOG_WARN("进程 {}(pid={}) 退出，退出码 {}", proc.name, pid, exit_code);
    }
}

void ProcessManager::maybe_start(Process& proc) {
    if (proc.pid > 0 || proc.stop_requested) {
        return;
    }

    if (!proc.restart_unlimited && proc.start_count >= proc.max_start_count) {
        if (!proc.exhausted) {
            proc.exhausted = true;
            LOG_WARN("进程 {} 已达到启动次数上限 {}，不再重启", proc.name, proc.max_start_count);
        }
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < proc.next_start) {
        return;
    }

    if (!dependencies_running(proc)) {
        return;
    }

    LOG_INFO("启动进程 {}: {}", proc.name, proc.description());
    std::string error;
    if (!proc.spawn(error)) {
        LOG_ERROR("启动进程 {} 失败: {}", proc.name, error);
        // 失败的启动同样计入次数，等一个间隔再重试，避免高频重试刷日志
        proc.next_start = now + proc.start_interval;
        return;
    }
    LOG_INFO("进程 {} 启动成功, pid={}", proc.name, proc.pid);
}

bool ProcessManager::dependencies_running(const Process& proc) const {
    for (const auto& depend : proc.depends) {
        const auto iter = index_.find(depend);
        if (iter == index_.end() || iter->second->pid <= 0) {
            return false;
        }
    }
    return true;
}

void ProcessManager::shutdown() {
    // 进程集合按依赖层级排好序，反向遍历即先停依赖方
    for (auto iter = processes_.rbegin(); iter != processes_.rend(); ++iter) {
        Process& proc = **iter;
        if (proc.pid <= 0) {
            continue;
        }
        proc.stop_requested = true;
        LOG_INFO("请求停止进程 {}(pid={})", proc.name, proc.pid);
        proc.terminate();
    }

    const auto deadline = std::chrono::steady_clock::now() + stop_timeout_;
    while (running_count() > 0 && std::chrono::steady_clock::now() < deadline) {
        reap_children();
        if (running_count() == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    for (auto& proc : processes_) {
        if (proc->pid <= 0) {
            continue;
        }
        LOG_WARN("进程 {} 未在 {} 秒内退出，强制终止", proc->name, stop_timeout_.count());
        proc->kill_force();
    }

    // 回收被强制终止的进程，避免留下僵尸进程
    for (int attempt = 0; attempt < 40 && running_count() > 0; ++attempt) {
        reap_children();
        if (running_count() == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

Process* ProcessManager::find_by_pid(ProcessId pid) {
    if (pid <= 0) {
        return nullptr;
    }
    for (auto& proc : processes_) {
        if (proc->pid == pid) {
            return proc.get();
        }
    }
    return nullptr;
}

std::size_t ProcessManager::running_count() const {
    std::size_t count = 0;
    for (const auto& proc : processes_) {
        if (proc->pid > 0) {
            ++count;
        }
    }
    return count;
}

}  // namespace ems::daemon
