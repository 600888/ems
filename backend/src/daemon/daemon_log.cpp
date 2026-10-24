#include "daemon_log.h"

#include <mutex>

#include "log/log_init.hpp"

namespace ems::daemon {
namespace {

/// 日志器名称，同时也是 conf/log.json 中的模块名。
constexpr const char* kLoggerName = "daemon";

std::once_flag g_logger_once;

}  // namespace

void init_daemon_logger(bool terminal) {
    std::call_once(g_logger_once, [terminal]() {
        // LogInitializer 内部完成 sink 创建、线程池初始化和默认日志器注册。
        const LogInitializer initializer(kLoggerName, LV::info, terminal, 1);
    });
}

}  // namespace ems::daemon
