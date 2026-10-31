#pragma once
#include <string>

namespace ems::web {
/** @brief 从真实终端读取可见文本并转换为 UTF-8。
 * @param[in] prompt 提示。 @return 原样文本，终端错误抛异常。
 */
std::string read_host_line(const std::string& prompt);
/** @brief 从真实终端读取不回显密码；不接受重定向。
 * @param[in] prompt 提示。 @return UTF-8 密码；终端错误抛异常。
 */
std::string read_host_password(const std::string& prompt);
/** @brief 独占创建受限摘要密钥文件，不覆盖现有文件。
 * @param[in] path 输出路径；Linux 权限 0600，Windows 私有 DACL。
 */
void create_identity_key(const std::string& path);
}  // namespace ems::web
