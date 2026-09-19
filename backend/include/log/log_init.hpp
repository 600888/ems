#pragma once
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <filesystem>
#include "spdlog/async.h"
#include "spdlog/spdlog.h"
// #include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <nlohmann/json.hpp>

#include "util/conf.hpp"

// 编译release、链接时优化选项增加速度：-Wall -pthread -O3 -flto -DNDEBUG

#ifdef DEBUG
// 在spdlog.h之前定义才有效
#ifndef SPDLOG_TRACE_ON
#define SPDLOG_TRACE_ON
#endif

#ifndef SPDLOG_DEBUG_ON
#define SPDLOG_DEBUG_ON
#endif

#endif

// Linux使用CLOCK_REALTIME_COARSE
#ifndef SPDLOG_CLOCK_COARSE
#define SPDLOG_CLOCK_COARSE
#endif

using LV = spdlog::level::level_enum;

struct LogInfo {
    LV level = LV::debug;
    long long fileSize = 1024 * 1024 * 10 * 10;
    int count = 10;
};

class LogInitializer {
public:
    LogInitializer(const std::string& defaultLoggerName,
                   LV defalutLogLevel = LV::debug,
                   bool enableTerminalLog = false,
                   const int threadNum = 1) noexcept
    {
        static std::once_flag flag = std::once_flag();
        std::call_once(flag, init, defaultLoggerName, defalutLogLevel, enableTerminalLog, threadNum);
    }

private:
    LogInitializer(const LogInitializer&) = delete;
    LogInitializer& operator=(const LogInitializer&) = delete;

    // 读取日志配置文件
    static inline std::unordered_map<std::string, LogInfo> _readLogConf()
    {
        std::unordered_map<std::string, LogInfo> logConfMap;
        std::unordered_map<std::string, LV> logLevelMap {
            { "debug", LV::debug },       { "info", LV::info }, { "trace", LV::critical }, { "error", LV::err },
            { "critical", LV::critical }, { "warn", LV::warn }, { "off", LV::off }
        };

        try {
            std::ifstream in(logConfPath(), std::ios::in);
            if (!in.is_open()) {
                std::cerr << "无法打开日志配置文件: " << logConfPath() << std::endl;
                return logConfMap;
            }
            std::string jsonContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            nlohmann::json jsonDoc = nlohmann::json::parse(jsonContent);

            // 遍历所有模块并打印其级别和文件大小
            if (jsonDoc.contains("modules") && jsonDoc["modules"].is_object()) {
                LogInfo logInfo;
                for (auto it = jsonDoc["modules"].begin(); it != jsonDoc["modules"].end(); ++it) {
                    std::string modulesStr = it.key();
                    auto& mod = it.value();
                    if (mod.contains("level") && mod["level"].is_string()) {
                        std::string levelStr = mod["level"].get<std::string>();
                        if (auto isFind = logLevelMap.find(levelStr) != logLevelMap.end()) {
                            logInfo.level = logLevelMap[levelStr];
                        }
                    }
                    if (mod.contains("file_size") && mod["file_size"].is_number_integer()) {
                        auto fileSize = mod["file_size"].get<int>();
                        logInfo.fileSize = fileSize;
                    }
                    if (mod.contains("count") && mod["count"].is_number_integer()) {
                        auto count = mod["count"].get<int>();
                        logInfo.count = count;
                    }
                    logConfMap[modulesStr] = logInfo;
                }
            }
        } catch (...) {
        }
        return logConfMap;
    }

    static inline void
    init(const std::string& defaultLoggerName, LV defalutLogLevel, bool enableTerminalLog, const int threadNum) noexcept
    {
        try {
            if (defaultLoggerName.empty() || spdlog::get(defaultLoggerName) != nullptr) {
                return;
            }

            // 读取配置文件
            std::unordered_map<std::string, LogInfo> logConfMap = _readLogConf();
            constexpr int oneMb = 1024 * 1024;
            long long fileSize = 10 * oneMb;
            LV logLevel = LV::debug;
            int count = 10;
            if (auto it = logConfMap.find(defaultLoggerName) != logConfMap.end()) {
                logLevel = logConfMap[defaultLoggerName].level;
                fileSize = logConfMap[defaultLoggerName].fileSize * oneMb;
                count = logConfMap[defaultLoggerName].count;
            }

            // 添加目的端
            std::vector<spdlog::sink_ptr> sinks;

            // 检查是否开启控制台
            if (enableTerminalLog) {
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }

            // 日志路径
            std::string logDir = logPath() + defaultLoggerName;
            std::filesystem::create_directories(logDir);

            std::string logFilePath = logDir + "/" + defaultLoggerName;
            if (logLevel == LV::debug) {
                logFilePath += "-debug.log";
            } else {
                logFilePath += ".log";
            }

            // 设置为滚动模式 
            // 当程序重启时，如果希望将之前的日志文件立即归档为备份文件，并开始写入新的日志文件，而不是继续写入之前的文件直到达到最大大小才旋转，就可以将 rotate_on_open 设置为 true 。
            auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFilePath, fileSize, count, false);
            sinks.push_back(rotating_sink);

            // 设置为异步模式
            spdlog::init_thread_pool(8192, threadNum); // 不要在其他地方再次调用init_thread_pool，否则会导致线程池析构
            std::shared_ptr<spdlog::logger> defaultLogger = std::make_shared<spdlog::async_logger>(
                defaultLoggerName, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::block);

            // register日志
            spdlog::register_logger(defaultLogger);

            // 设置日志等级
            defaultLogger->set_level(logLevel);

            // 设置日志格式[年-月-日 时:分:秒.毫秒][进程号:线程号][调试级别](宏定义中添加: [文件名:行号]<函数名> - 具体内容)
            defaultLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%P:%t][%l]%v");

            // 设置当出发debug或更严重的错误时立刻刷新日志到disk
            defaultLogger->flush_on(logLevel);

            spdlog::set_default_logger(defaultLogger);
        } catch (...) {
            spdlog::drop_all();
        }
    };
};