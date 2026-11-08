#pragma once

#include <common/database/mysql_database.h>
#include <common/database/timeseries/factory.h>

#include "sampling.h"

namespace ems::storage {
struct StorageOptions {
    database::MySqlConnectionOptions database;
    database::TimeSeriesConnectionOptions timeseries;
    std::string realtime_config;
    std::string scope_code;
    std::int64_t interval_ms{kDefaultArchiveIntervalMs};
};

/**
 * @brief 读取存储配置，相对依赖文件以此配置所在目录解析。
 * @param[in] path UTF-8 JSON 文件路径；未知字段及不合法间隔直接拒绝。
 * @return 经过结构校验的选项；不连接数据库或实时库，错误返回 InvalidArgument。
 */
core::Result<StorageOptions> load_storage_options(const std::string& path);
}  // namespace ems::storage
