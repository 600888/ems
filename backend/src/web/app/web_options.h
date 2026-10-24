#pragma once
#include <common/database/mysql_database.h>

#include "web/transport/json.h"

namespace ems::web {
struct WebOptions {
    std::string host{"127.0.0.1"};
    unsigned short port{8080};
    std::string allowed_origin{"http://127.0.0.1:8080"};
    std::string database_config;
    std::string document_root;
    std::string workspace{"web-upload"};
    std::size_t io_threads{2}, workers{4}, queue_size{128}, import_workers{1}, import_queue_size{8};
    int request_timeout_ms{5000}, import_timeout_ms{30000}, session_ttl_seconds{3600};
    std::size_t session_capacity{1000};
    bool secure_cookie{false};
    Json users;
};

/** @brief 读取严格 Web 配置，账号可由 EMS_WEB_IDENTITY_FILE 提供。
 * @param[in] path UTF-8 配置路径。
 * @return 已校验选项；不连接数据库。
 */
WebOptions load_web_options(const std::string& path);
/** @brief 装配并运行 Web，返回前所有工作线程已退出。
 * @param[in] options 已校验配置。
 * @return 正常退出为 0，启动错误抛异常。
 */
int run_web(const WebOptions& options);
/** @brief 使用显式数据库上下文运行 HTTP 入口，允许集成测试注入公共测试驱动。
 * @param[in] options 已校验运行选项。
 * @param[in] database 独立公共数据库上下文。
 * @return 正常退出为 0。
 */
int run_web_with_database(const WebOptions& options, database::MySqlDatabaseContext database);
}  // namespace ems::web
