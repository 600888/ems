#pragma once

#include <common/database/timeseries/store.h>

#include <memory>

namespace ems::database {

struct TaosConnectionOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{6030};
    std::string user;
    std::string password;
    std::string database{"ems_history"};
    std::string config_dir;
};

enum class TimeSeriesBackend { Tdengine, InfluxDb };

struct InfluxDbConnectionOptions {
    std::string url{"http://127.0.0.1:8086"};
    std::string database{"ems_history"};  ///< InfluxDB 2.9 中指向 bucket 的默认 DBRP 数据库名
    std::string token;
    std::uint32_t timeout_ms{10000};
};

struct TimeSeriesConnectionOptions {
    TimeSeriesBackend backend{TimeSeriesBackend::Tdengine};
    TaosConnectionOptions taos;
    InfluxDbConnectionOptions influxdb;
};

/**
 * @brief 读取共享时序连接配置；对应后端的环境变量覆盖 JSON，不连接数据库。
 * @param[in] path 可选 JSON 文件；为空时使用 EMS_TIMESERIES_CONFIG 或纯环境配置。
 * @return 已校验的连接参数；非法文件或所选后端缺少必要参数返回 InvalidArgument。
 */
core::Result<TimeSeriesConnectionOptions> load_time_series_options(const std::string& path = {});

/**
 * @brief 建立目标 TDengine 3.4.3 时序写入连接；不自动执行迁移。
 * @param[in] options TDengine 原生 C/C++ 客户端连接选项。
 * @return 成功返回时序存储所有权；失败返回脱敏连接错误。
 */
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_taos_series(
    const TaosConnectionOptions& options);

/**
 * @brief 通过 influxdb-cxx 建立 InfluxDB 2.9 兼容接口后端并验证查询权限。
 * @param[in] options 服务 URL、DBRP 数据库名、API token 与请求超时；不创建 bucket 或映射。
 * @return 成功返回后端所有权；失败返回脱敏参数、连接或权限错误。
 */
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_influxdb_series(
    const InfluxDbConnectionOptions& options);

/**
 * @brief 解析进程配置中的时序后端名称。
 * @param[in] name 后端名称，支持 taos、tdengine 和 influxdb。
 * @return 对应后端类型；未知名称返回 InvalidArgument。
 */
core::Result<TimeSeriesBackend> parse_time_series_backend(const std::string& name);

/**
 * @brief 根据配置创建统一的时序读写后端。
 * @param[in] options 后端类型及对应连接配置。
 * @return 成功返回后端所有权；未编译的后端返回 Unsupported，连接失败返回脱敏错误。
 */
core::Result<std::unique_ptr<ITimeSeriesDatabase>> connect_time_series(
    const TimeSeriesConnectionOptions& options);

}  // namespace ems::database
