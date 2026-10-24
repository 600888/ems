#pragma once

#include <common/database/configuration/i_device_config_repository.h>
#include <common/database/configuration/i_device_management_store.h>
#include <common/database/configuration/repository.h>
#include <common/database/history/repositories.h>
#include <common/database/i_database.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace ems::database {

struct MySqlConnectionOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string user;
    std::string password;
    std::string database;
    std::size_t max_connections{4};
    std::chrono::milliseconds acquire_timeout{1000};
    unsigned int io_timeout_seconds{5};
    bool require_history_schema{false};
    bool require_configuration_schema{false};
};

/** @brief 同一进程的一组 Repository；共同使用 ormpp 进程内连接池与事务上下文。 */
struct MySqlDatabaseContext {
    std::shared_ptr<IDatabase> database;
    std::shared_ptr<IHistoryCatalogRepository> history_catalog;
    std::shared_ptr<IHistoryEventRepository> history_events;
    std::shared_ptr<IConfigurationRepository> configuration;
    std::shared_ptr<IDeviceConfigRepository> device_config;
    std::shared_ptr<IDeviceManagementStore> device_management;
};

/**
 * @brief 初始化或复用 ormpp 进程内池并验证连接，可选检查历史 schema；不自动建表。
 * @param[in] options 首次初始化的凭据、连接数及预算；后续配置必须一致。
 * @param[in] ctx 首次连接与 schema 检查期限。
 * @return 共享池的上下文；不同池配置返回 Conflict，未编译驱动返回 Unsupported，
 * schema 不兼容拒绝就绪。池随进程存活，各进程互不共享连接。
 */
core::Result<MySqlDatabaseContext> connect_mysql_database(const MySqlConnectionOptions& options,
                                                          const core::CallContext& ctx);

/**
 * @brief 读取公共 MySQL 配置；不连接数据库。
 * @param[in] path 可选 JSON 文件，空值时读取 EMS_DATABASE_CONFIG 指定文件；未指定时仅用环境变量。
 * @return 连接配置；EMS_MYSQL_* 覆盖 JSON，非法文件或参数返回 InvalidArgument，不静默回退。
 */
core::Result<MySqlConnectionOptions> load_mysql_options(const std::string& path = {});

}  // namespace ems::database
