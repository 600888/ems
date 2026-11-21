#pragma once

#include <rtdb/client/command_client.h>

#include <functional>

#include "web/modules/device/application/device_service.h"
#include "web/modules/history/history_service.h"

namespace ems::web {

struct RuntimeOptions {
    std::string realtime_config;
    std::string scope_code{"station001"};
    std::string timeseries_config;
    /// 命令就绪提示端点；空串时依靠采集侧扫描，命令目标和结果始终读写实时库。
    std::string command_endpoint;
};

class RuntimeService {
   public:
    using ReadPoint = std::function<core::Result<std::string>(
        const std::string&, const std::string&, const core::CallContext&)>;
    using ReadHistory = HistoryService::Reader;

    /**
     * @brief 装配实时库命令、测点与历史查询入口；测试可注入测点读者。
     * @param[in] database 共享数据库上下文。
     * @param[in] options 实时库部署配置。
     * @param[in] reader 可选实时读取函数，不持有设备执行权限。
     * @param[in] history_reader 可选历史读取函数；留空时连接部署的时序库。
     */
    RuntimeService(database::MySqlDatabaseContext database, RuntimeOptions options,
                   ReadPoint reader = {}, ReadHistory history_reader = {});
    /**
     * @brief 执行授权测点查询；历史读取不持有工程锁。
     * @param[in] path 固定 Web 路由。
     * @param[in] body 请求字段。
     * @param[in] actor 当前会话主体。
     * @param[in] ctx 调用期限。
     * @return 查询结果；服务缺失时返回明确不可用错误，不模拟执行或采样。
     */
    DeviceResult execute(const std::string& path, const Json& body, const Principal& actor,
                         const core::CallContext& ctx);

   private:
    database::MySqlDatabaseContext database_;
    RuntimeOptions options_;
    ReadPoint reader_;
    ReadHistory history_reader_;
    std::shared_ptr<rtdb::CommandClient> command_client_;
};
}  // namespace ems::web
