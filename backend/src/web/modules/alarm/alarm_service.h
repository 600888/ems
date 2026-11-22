#pragma once

#include <common/database/timeseries/store.h>

#include <functional>

#include "web/modules/device/application/device_service.h"

namespace ems::web {

class AlarmService {
   public:
    using SeriesConnection = core::Result<std::unique_ptr<database::ITimeSeriesDatabase>>;
    using OpenSeries = std::function<SeriesConnection()>;

    /**
     * @brief 装配告警 Web 模块；连接时序库延迟到历史或详情请求。
     * @param[in] database 公共数据库上下文。
     * @param[in] timeseries_config 共享时序配置路径。
     * @param[in] realtime_config 实时配置路径，供详情读取关联点。
     * @param[in] open_series 可选测试时序连接工厂。
     */
    AlarmService(database::MySqlDatabaseContext database, std::string timeseries_config,
                 std::string realtime_config, OpenSeries open_series = {});

    /**
     * @brief 执行告警查询、导出、详情或首次确认。
     * @param[in] path 已注册路由。
     * @param[in] body JSON 业务参数。
     * @param[in] actor 当前会话主体。
     * @param[in] ctx 共享请求期限。
     * @return HTTP 业务结果；失败抛出 ApiError。
     */
    DeviceResult execute(const std::string& path, const Json& body, const Principal& actor,
                         const core::CallContext& ctx);

   private:
    database::MySqlDatabaseContext database_;
    std::string timeseries_config_;
    std::string realtime_config_;
    OpenSeries open_series_;
};

}  // namespace ems::web
