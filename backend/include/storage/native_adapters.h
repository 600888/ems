#pragma once

#include <common/core/result.h>
#include <storage/storage.h>

#include <cstdint>
#include <memory>
#include <string>

namespace ems::storage {

struct MySqlConnectionOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{3306};
    std::string user;
    std::string password;
    std::string database;
};

struct TaosConnectionOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6030};
    std::string user;
    std::string password;
    std::string database{"ems_history"};
};

struct RedisStreamOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6379};
    std::string user;
    std::string password;
    std::string stream_key;
    std::string group{"history-storage"};
};

/**
 * @brief 建立 MySQL 历史仓储；进程拥有返回对象，不自动执行迁移。
 * @param[in] options MySQL 连接选项。
 * @return 成功返回仓储所有权；失败返回脱敏连接错误。
 */
core::Result<std::unique_ptr<IHistoryRepository>> connect_mysql_history(
    const MySqlConnectionOptions& options);

/**
 * @brief 建立目标 TDengine 3.4.2 时序写入连接；不自动执行迁移。
 * @param[in] options TDengine 原生 C/C++ 客户端连接选项。
 * @return 成功返回时序存储所有权；失败返回脱敏连接错误。
 */
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_taos_series(
    const TaosConnectionOptions& options);

/**
 * @brief 建立单作用域 Redis telemetry Stream 消费连接并确保消费组存在。
 * @param[in] options Redis 地址、完整 Stream 键及消费组。
 * @return 成功返回事件总线所有权；失败返回脱敏连接错误。
 */
core::Result<std::unique_ptr<messaging::IEventBus>> connect_redis_stream(
    const RedisStreamOptions& options);

}  // namespace ems::storage
