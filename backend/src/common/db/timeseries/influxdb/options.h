#pragma once

#include <common/database/timeseries/factory.h>

#include <regex>

namespace ems::database::influx {

/**
 * @brief 校验 influxdb-cxx HTTP 服务地址、DBRP 数据库名、凭据及请求预算。
 * @param[in] options InfluxDB 连接配置。
 * @return 参数完整且服务 URL 不含用户信息、查询、片段或控制字符时返回 true。
 */
inline bool valid_options(const InfluxDbConnectionOptions& options) {
    static const std::regex url(
        R"(^https?://(\[[0-9a-fA-F:.]+\]|[A-Za-z0-9._-]+)(:([0-9]{1,5}))?(/[A-Za-z0-9._~!$&'()*+,;=:%/-]*)?$)");
    std::smatch match;
    if (!std::regex_match(options.url, match, url) || options.database.empty() ||
        options.database.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") !=
            std::string::npos ||
        options.token.empty() || options.timeout_ms == 0 || options.timeout_ms > 300000)
        return false;
    if (match[3].matched && (std::stoul(match[3]) == 0 || std::stoul(match[3]) > 65535))
        return false;
    for (const auto ch : options.token)
        if (static_cast<unsigned char>(ch) <= 32 || static_cast<unsigned char>(ch) >= 127)
            return false;
    return true;
}

}  // namespace ems::database::influx
