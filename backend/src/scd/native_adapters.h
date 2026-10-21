#pragma once

// 过渡头：不重新声明数据库类型或创建进程私有驱动。
#include <common/database/mysql_database.h>
#include <common/database/timeseries/factory.h>

namespace ems::storage {
using database::connect_time_series;
using database::MySqlConnectionOptions;
using database::parse_time_series_backend;
using database::TaosConnectionOptions;
using database::TimeSeriesBackend;
using database::TimeSeriesConnectionOptions;
}  // namespace ems::storage
