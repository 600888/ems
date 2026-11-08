#pragma once

#include <common/database/timeseries/types.h>

#include <functional>

#include "web/modules/device/application/device_service.h"

namespace ems::web {
/** 历史读取独立于实时监视；仅消费已授权的设备配置和时序读取端口。 */
class HistoryService {
   public:
    using Reader = std::function<core::Result<std::vector<database::Sample>>(
        const database::HistoryQuery&, const std::vector<database::SeriesMapping>&, std::uint32_t,
        const core::CallContext&)>;

    /**
     * @brief 装配历史查询依赖，不连接实时库。
     * @param[in] database 配置、授权和历史目录数据库上下文。
     * @param[in] reader 可选时序读取端口；为空时按部署环境连接 TDengine。
     * @param[in] timeseries_config 可选共享时序 JSON 配置路径，环境变量可覆盖。
     */
    explicit HistoryService(database::MySqlDatabaseContext database, Reader reader = {},
                            std::string timeseries_config = {});

    /**
     * @brief 查询设备的遥测或遥信历史，按需执行有界时间桶采样。
     * @param[in] body 查询条件；时间范围为左闭右开，最多 30 天。
     * @param[in] actor 会话主体，必须具有 history.read 和设备作用域权限。
     * @param[in] ctx 调用期限。
     * @return 样本及截断标记；无映射返回空集，依赖不可用或数据异常时抛 ApiError。
     */
    DeviceResult execute(const Json& body, const Principal& actor, const core::CallContext& ctx);

   private:
    database::MySqlDatabaseContext database_;
    Reader reader_;
    std::string timeseries_config_;
};
}  // namespace ems::web
