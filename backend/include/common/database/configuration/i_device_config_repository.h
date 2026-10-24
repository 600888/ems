/**
 * @file i_device_config_repository.h
 * @brief 设备管理配置仓储：整树读取、按稳定 ID 增删改、修订号递增。
 *
 * 设计见 website/docs/common/database/device-management.md：
 * 不做配置版本，表可原地修改；调用方把“写配置 + 递增 config_revision”
 * 放在同一个事务里，消费者按修订号热加载。
 */
#pragma once

#include <common/core/result.h>
#include <common/database/configuration/device_management.h>
#include <common/database/i_database.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ems::database {

/// 四遥点所在表
enum class PointTableKind : std::uint8_t { Yc = 0, Yx = 1, Yk = 2, Yt = 3 };

/// 任意一张四遥点表的行
using AnyPointRow = std::variant<CfgPointYcRow, CfgPointYxRow, CfgPointYkRow, CfgPointYtRow>;

/// 一次读出的完整工程配置（不含运行统计）
struct DeviceConfigRecords {
    std::vector<CfgDeviceRow> devices;
    std::vector<CfgDeviceTypeRow> device_types;
    std::vector<CfgTypePointRow> type_points;
    std::vector<CfgChannelRow> channels;
    std::vector<CfgBindingRow> bindings;
    std::vector<CfgPointYcRow> points_yc;
    std::vector<CfgPointYxRow> points_yx;
    std::vector<CfgPointYkRow> points_yk;
    std::vector<CfgPointYtRow> points_yt;
};

/**
 * @brief 设备管理配置仓储。
 *
 * 所有方法使用调用方事务：不提交、不回滚、不另开连接。写方法只做单表
 * upsert/delete，跨表校验（固有点必需项、地址冲突、四遥类型一致）由配置
 * 服务在调用前完成；数据库唯一键与 CHECK 是最后一道防线。
 */
class IDeviceConfigRepository {
   public:
    /** @brief 释放仓储接口。 */
    virtual ~IDeviceConfigRepository() = default;

    /**
     * @brief 读取全部设备配置与类型目录。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] ctx 调用期限。
     * @return 全量记录；单表超过上限返回 ResourceExhausted，数据库错误不伪装成空表。
     */
    virtual core::Result<DeviceConfigRecords> load_configuration(ITransaction& tx,
                                                                 const core::CallContext& ctx) = 0;

    /**
     * @brief 按稳定 ID 新增或更新设备节点。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] row 设备行；`device_id` 必填。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；唯一键冲突返回 Conflict。
     */
    virtual core::Status save_device(ITransaction& tx, const CfgDeviceRow& row,
                                     const core::CallContext& ctx) = 0;

    /**
     * @brief 删除设备节点。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] device_id 稳定设备 ID。
     * @param[in] ctx 调用期限。
     * @return 结果；仍有子节点或绑定时返回 Conflict，不存在返回 NotFound。
     */
    virtual core::Status remove_device(ITransaction& tx, const std::string& device_id,
                                       const core::CallContext& ctx) = 0;

    /**
     * @brief 按稳定 ID 新增或更新通道。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] row 通道行；`channel_id` 必填。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；编码冲突返回 Conflict。
     */
    virtual core::Status save_channel(ITransaction& tx, const CfgChannelRow& row,
                                      const core::CallContext& ctx) = 0;

    /**
     * @brief 删除通道。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] channel_id 稳定通道 ID。
     * @param[in] ctx 调用期限。
     * @return 结果；仍被绑定引用时返回 Conflict。
     */
    virtual core::Status remove_channel(ITransaction& tx, const std::string& channel_id,
                                        const core::CallContext& ctx) = 0;

    /**
     * @brief 按稳定 ID 新增或更新设备-通道绑定。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] row 绑定行；`binding_id` 必填。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；同设备同通道重复返回 Conflict。
     */
    virtual core::Status save_binding(ITransaction& tx, const CfgBindingRow& row,
                                      const core::CallContext& ctx) = 0;

    /**
     * @brief 删除绑定。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] binding_id 稳定绑定 ID。
     * @param[in] ctx 调用期限。
     * @return 结果；仍被四遥点引用时返回 Conflict。
     */
    virtual core::Status remove_binding(ITransaction& tx, const std::string& binding_id,
                                        const core::CallContext& ctx) = 0;

    /**
     * @brief 按稳定 ID 新增或更新四遥点（按行类型写入对应表）。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] row 点行；`point_id` 必填。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；code/厂家键/固有点重复返回 Conflict。
     */
    virtual core::Status save_point(ITransaction& tx, const AnyPointRow& row,
                                    const core::CallContext& ctx) = 0;

    /**
     * @brief 从指定点表删除点。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] kind 目标点表。
     * @param[in] point_id 稳定点 ID。
     * @param[in] ctx 调用期限。
     * @return 结果；不存在返回 NotFound。
     */
    virtual core::Status remove_point(ITransaction& tx, PointTableKind kind,
                                      const std::string& point_id,
                                      const core::CallContext& ctx) = 0;

    /**
     * @brief 递增站根修订号，供消费者热加载。
     * @param[in,out] tx 本上下文活动事务。
     * @param[in] station_device_id 站根设备 ID。
     * @param[in] ctx 调用期限。
     * @return 递增后的修订号；站根不存在返回 NotFound。
     */
    virtual core::Result<std::int64_t> bump_config_revision(ITransaction& tx,
                                                            const std::string& station_device_id,
                                                            const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
