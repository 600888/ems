#pragma once

#include "log/log_init.hpp"

/** @brief 管理进程默认日志器，退出作用域时等待异步日志写完。 */
class ProcessLogger {
   public:
    /**
     * @brief 按进程名称读取 conf/log.json，并初始化独立日志文件。
     * @param[in] name 可执行文件名，同时作为配置模块名和日志目录名。
     * @param[in] level 配置缺失时使用的日志等级。
     * @param[in] terminal 是否同时输出到终端。
     */
    explicit ProcessLogger(const std::string& name, LV level = LV::info,
                           bool terminal = false) noexcept {
        const LogInitializer initializer(name, level, terminal);
    }

    /** @brief 等待异步日志队列排空，并关闭日志线程和文件。 */
    ~ProcessLogger() { spdlog::shutdown(); }

    /**
     * @brief 禁止复制进程日志生命周期。
     * @param[in] other 源对象。
     */
    ProcessLogger(const ProcessLogger& other) = delete;

    /**
     * @brief 禁止赋值进程日志生命周期。
     * @param[in] other 源对象。
     * @return 不允许调用。
     */
    ProcessLogger& operator=(const ProcessLogger& other) = delete;
};
