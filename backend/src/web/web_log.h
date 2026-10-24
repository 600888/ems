#pragma once

namespace ems::web {

/**
 * @brief 将 Drogon/Trantor 日志接入已初始化的进程默认日志器。
 *
 * 在启动框架线程前调用，沿用默认日志器的等级、文件和滚动配置。
 */
void configure_framework_logging();

}  // namespace ems::web
