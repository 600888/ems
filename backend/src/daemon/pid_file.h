#pragma once

#include <string>

namespace ems::daemon {

/**
 * @brief 确保 pid 文件所在目录存在（跨平台）。
 * @param[in] dir 目录绝对路径。
 * @param[out] error 失败原因。
 * @return 目录可用返回 true。
 */
bool ensure_pid_directory(const std::string& dir, std::string& error);

}  // namespace ems::daemon
