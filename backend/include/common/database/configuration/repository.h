#pragma once

#include <common/database/configuration/channel.h>
#include <common/database/configuration/device.h>
#include <common/database/configuration/point_table.h>
#include <common/database/i_database.h>

namespace ems::database {

struct ConfigurationRecords {
    std::vector<DeviceRecord> devices;
    std::vector<ChannelRecord> channels;
    PointTableData points;
};

enum class RecordWriteMode { Insert, Update };

/** @brief 工程配置仓储；所有读写使用调用方事务，不提交、不发布运行版本。 */
class IConfigurationRepository {
   public:
    /** @brief 释放仓储接口。 */
    virtual ~IConfigurationRepository() = default;

    /**
     * @brief 读取设备、通道及四遥配置，每张表最多 10000 行。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] ctx 调用期限。
     * @return 配置记录；超出上限返回 ResourceExhausted，数据库错误不会伪装成空表。
     */
    virtual core::Result<ConfigurationRecords> read_configuration(ITransaction& tx,
                                                                  const core::CallContext& ctx) = 0;

    /**
     * @brief 按ID读取设备。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] id 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录；不存在返回 NotFound，非法标识返回 InvalidArgument。
     */
    virtual core::Result<DeviceRecord> find_device(ITransaction& tx, std::int64_t id,
                                                   const core::CallContext& ctx) = 0;

    /**
     * @brief 按编码读取设备。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] code 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录；不存在返回 NotFound，非法标识返回 InvalidArgument。
     */
    virtual core::Result<DeviceRecord> find_device_by_code(ITransaction& tx,
                                                           const std::string& code,
                                                           const core::CallContext& ctx) = 0;

    /**
     * @brief 新增或更新设备配置。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] record 待保存记录。
     * @param[in] mode 明确选择新增或更新。
     * @param[in] ctx 调用期限。
     * @return 暂存结果，提交由调用方负责；编码冲突返回 Conflict。
     */
    virtual core::Status write_device(ITransaction& tx, const DeviceRecord& record,
                                      RecordWriteMode mode, const core::CallContext& ctx) = 0;

    /**
     * @brief 删除设备，仍被配置引用时拒绝。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] id 待删除记录 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；不存在为 NotFound，被引用为 Conflict。
     */
    virtual core::Status remove_device(ITransaction& tx, std::int64_t id,
                                       const core::CallContext& ctx) = 0;

    /**
     * @brief 按ID读取通道。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] id 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录；不存在返回 NotFound，非法标识返回 InvalidArgument。
     */
    virtual core::Result<ChannelRecord> find_channel(ITransaction& tx, std::int64_t id,
                                                     const core::CallContext& ctx) = 0;

    /**
     * @brief 按编码读取通道。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] code 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录；不存在返回 NotFound，非法标识返回 InvalidArgument。
     */
    virtual core::Result<ChannelRecord> find_channel_by_code(ITransaction& tx,
                                                             const std::string& code,
                                                             const core::CallContext& ctx) = 0;

    /**
     * @brief 新增或更新通道配置。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] record 待保存记录。
     * @param[in] mode 明确选择新增或更新。
     * @param[in] ctx 调用期限。
     * @return 暂存结果，提交由调用方负责；编码冲突返回 Conflict。
     */
    virtual core::Status write_channel(ITransaction& tx, const ChannelRecord& record,
                                       RecordWriteMode mode, const core::CallContext& ctx) = 0;

    /**
     * @brief 删除通道，仍被配置引用时拒绝。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] id 待删除记录 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；不存在为 NotFound，被引用为 Conflict。
     */
    virtual core::Status remove_channel(ITransaction& tx, std::int64_t id,
                                        const core::CallContext& ctx) = 0;

    /**
     * @brief 在同一事务中校验并暂存整批四遥点表，不进行设备控制。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] data 解析后的点表草稿，仍须验证作用域、协议和反馈。
     * @param[in] channel_id 目标通道 ID。
     * @param[in] device_id 目标设备 ID。
     * @param[in] mode 追加或替换目标通道。
     * @param[in] ctx 调用期限。
     * @return 带生成 ID 的暂存数据；失败必须回滚，成功提交后 ID 才有效。
     */
    virtual core::Result<PointTableData> stage_point_table(
        ITransaction& tx, const PointTableData& data, std::int64_t channel_id,
        std::int64_t device_id, PointTableImportMode mode, const core::CallContext& ctx) = 0;
};

/**
 * @brief 解析 XLSX 并通过公共仓储提交点表；文件解析期间不占用数据库连接。
 * @param[in,out] database 公共事务入口。
 * @param[in,out] repository 同上下文配置仓储。
 * @param[in] path UTF-8 XLSX 文件路径。
 * @param[in] channel_id 目标通道 ID。
 * @param[in] device_id 目标设备 ID。
 * @param[in] mode 追加或替换目标通道。
 * @param[in] ctx 解析、数据库读写和提交的总调用期限。
 * @return 解析结果或数据库错误；提交 Unknown 不得自动重试。
 */
core::Result<PointTableImportResult> import_configuration_points(
    IDatabase& database, IConfigurationRepository& repository, const std::string& path,
    std::int64_t channel_id, std::int64_t device_id, PointTableImportMode mode,
    const core::CallContext& ctx);

}  // namespace ems::database
