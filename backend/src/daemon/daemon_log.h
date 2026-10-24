#pragma once

namespace ems::daemon {

/**
 * @brief 初始化守护进程日志器。
 *
 * 复用公共日志初始化：级别、单文件大小和滚动个数取自 conf/log.json 中的
 * "ems-daemon" 模块，缺失时使用代码默认值；日志目录为 <EMS_ROOT>/log/ems-daemon/。
 * 初始化后默认日志器即为守护进程日志器，业务代码直接用 LOG_xxx 宏。
 * @param[in] terminal 是否同时输出到终端，前台运行时为 true。
 */
void init_daemon_logger(bool terminal);

}  // namespace ems::daemon
