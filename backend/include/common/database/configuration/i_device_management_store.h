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
};
}  // namespace ems::database
