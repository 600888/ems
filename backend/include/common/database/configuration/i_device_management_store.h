#pragma once

#include <common/database/configuration/i_device_config_repository.h>

namespace ems::database {

/// 设备管理查询的固定表集合，禁止调用方提供 SQL 表名。
enum class DeviceTable {
    Device,
    Type,
    TypePoint,
    Template,
    Channel,
    Binding,
    Yc,
    Yx,
    Yk,
    Yt,
    ImportJob,
    ImportRow,
    CommStat,
    StateLog
};
using DeviceManagementRecord =
    std::variant<CfgDeviceRow, CfgDeviceTypeRow, CfgTypePointRow, CfgTemplateRow, CfgChannelRow,
                 CfgBindingRow, CfgPointYcRow, CfgPointYxRow, CfgPointYkRow, CfgPointYtRow,
                 CfgImportJobRow, CfgImportRow, OpsCommStat5mRow, OpsStateLogRow>;
enum class DeviceFilterOp { Equal, Like, Prefix };

struct DeviceFilter {
    std::string field;
    std::string value;
    DeviceFilterOp op{DeviceFilterOp::Equal};
};

struct DeviceQuery {
    DeviceTable table{DeviceTable::Device};
    std::vector<DeviceFilter> filters;
    std::string order_by;
    bool descending{false};
    std::size_t offset{0};
    std::size_t limit{200};
    bool for_update{false};
    bool for_share{false};
};

struct DevicePage {
    std::vector<DeviceManagementRecord> rows;
    std::int64_t total{0};
};

/// 设备管理分页、导入及锁入口；所有操作共用调用方事务，失败后禁止继续提交。
class IDeviceManagementStore {
   public:
    /** @brief 释放仓储。 */
    virtual ~IDeviceManagementStore() = default;
    /**
     * @brief 使用字段白名单与绑定参数读取分页；for_update 只用于短事务锁定。
     * @param[in,out] tx 同上下文事务。
     * @param[in] query 查询条件，最多八项，limit 为 1–100000。
     * @param[in] ctx 调用期限。
     * @return 行与真实计数；非法条件返回 InvalidArgument，数据库错误不伪装为空。
     */
    virtual core::Result<DevicePage> query(ITransaction& tx, const DeviceQuery& query,
                                           const core::CallContext& ctx) = 0;
    /**
     * @brief 暂存设备、通道、绑定、四遥或导入记录；不提交、不递增工程计数。
     * @param[in,out] tx 同上下文事务。
     * @param[in] row 记录；稳定身份和业务约束由调用服务校验。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；不支持写入类型目录或运行统计时返回 Unsupported。
     */
    virtual core::Status put(ITransaction& tx, const DeviceManagementRecord& row,
                             const core::CallContext& ctx) = 0;
    /**
     * @brief 在调用方事务内批量新增导入行，复用预编译语句，不查询或覆盖已有行。
     * @param[in,out] tx 同上下文事务，不在此接口提交。
     * @param[in] rows 新任务的行；id 必须为零，任务及行号必须满足数据库唯一约束。
     * @param[in] ctx 调用期限，分批检查期限。
     * @return 暂存结果；重复行或数据库失败使事务失效，调用方应整包回滚。
     */
    virtual core::Status append_import_rows(ITransaction& tx, const std::vector<CfgImportRow>& rows,
                                            const core::CallContext& ctx) = 0;
    /**
     * @brief 批量更新已读取的导入行，保持数据库身份与任务归属。
     * @param[in,out] tx 调用方事务，不在此提交。
     * @param[in] rows 含非零数据库 id 的已存在导入行。
     * @param[in] ctx 分批检查的调用期限。
     * @return 暂存状态；驱动错误使事务失效。
     */
    virtual core::Status update_import_rows(ITransaction& tx, const std::vector<CfgImportRow>& rows,
                                            const core::CallContext& ctx) = 0;
    /**
     * @brief 分批新增或更新已经过服务校验的四遥点，不查询或提交。
     * @param[in,out] tx 调用方事务。
     * @param[in] points 仅含四遥记录；id 为零时新增，否则按已读取的 id 更新。
     * @param[in] ctx 分批检查的调用期限。
     * @return 暂存状态；非法类型或数据库错误使事务失效，调用方应整包回滚。
     */
    virtual core::Status put_import_points(ITransaction& tx,
                                           const std::vector<DeviceManagementRecord>& points,
                                           const core::CallContext& ctx) = 0;
    /**
     * @brief 清空整站工程、导入记录及通信统计和状态，保留站根、类型目录及模板。
     * @param[in,out] tx 已锁定站根的调用方事务；不在此提交或递增配置修订号。
     * @param[in] station_id 必须与唯一站根的稳定 ID 一致。
     * @param[in] ctx 调用期限。
     * @return 暂存状态；站根不匹配或数据库失败时禁止提交，调用方应整事务回滚。
     */
    virtual core::Status clear_station(ITransaction& tx, const std::string& station_id,
                                       const core::CallContext& ctx) = 0;
};
}  // namespace ems::database
