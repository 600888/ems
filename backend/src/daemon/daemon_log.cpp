#include "daemon_log.h"

#include "log/process_logger.hpp"

namespace ems::daemon {
namespace {

/// 日志器名称，同时也是 conf/log.json 中的模块名。
constexpr const char* kLoggerName = "ems-daemon";

}  // namespace

void init_daemon_logger(bool terminal) {
    static const ProcessLogger logger(kLoggerName, LV::info, terminal);
}

}  // namespace ems::daemon
