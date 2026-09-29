#pragma once

#include <common/core/result.h>

#include <cstdint>
#include <memory>
#include <string>

#include "storage.h"

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

enum class TimeSeriesBackend { Tdengine, InfluxDb };

struct TimeSeriesConnectionOptions {
    TimeSeriesBackend backend{TimeSeriesBackend::Tdengine};
    TaosConnectionOptions taos;
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
 * @brief 解析进程配置中的时序后端名称。
 * @param[in] name 后端名称，支持 taos、tdengine 和 influxdb。
 * @return 对应后端类型；未知名称返回 InvalidArgument。
 */
core::Result<TimeSeriesBackend> parse_time_series_backend(const std::string& name);

/**
 * @brief 根据配置创建统一的时序读写后端。
 * @param[in] options 后端类型及对应连接配置。
 * @return 成功返回后端所有权；未实现的后端返回 Unsupported。
 */
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_time_series(
    const TimeSeriesConnectionOptions& options);

}  // namespace ems::storage
