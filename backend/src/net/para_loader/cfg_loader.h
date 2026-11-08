/** @file cfg_loader.h
 * @brief 从 cfg_device/cfg_channel/cfg_binding 和四遥新表编译采集配置。
 */
#pragma once

#include <common/database/mysql_database.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "rtdb/model/rtdb_channel.h"

namespace ems::net::device {

/** @brief 稳定测点身份到协议地址的运行时路由，不映射关系表。 */
struct PointRoute {
    std::string point_id;
    std::string protocol_code;
    double scale_factor{1.0};
    double offset_value{0.0};
    bool invert{false};
    std::string write_mode;
    double fixed_raw_value{0.0};
};

/** @brief 通道内的点归属节点；部件点保留部件身份，使用来源设备的绑定通信。 */
struct ConfiguredDevice {
    std::int64_t channelId_{0};  ///< 运行时所属逻辑通道，用于共享连接内独立停止调度。
    int pollIntervalMs_{0};      ///< 独立轮询周期；0 表示沿用连接工作线程的周期。
    std::int64_t deviceId_{0};
    std::string deviceKey_;
    std::string deviceName_;
    std::vector<PointRoute> ycPoints_, yxPoints_, ykPoints_, ytPoints_;
};

/** @brief RTDB 标准点到固定服务端模型的单向映射；值已是工程值，不重复缩放。 */
struct ForwardingPoint {
    std::string code, source_point_id, point_type, mms_type;
    int stale_after_ms{60000};
    bool source_enabled{false};
};

/** @brief 与采集线程分开的只读 MMS 上送通道。 */
struct ForwardingChannelConfig {
    ems::rtdb::ChannelConfig channel;
    std::vector<ForwardingPoint> points;
};

/** @brief 新设备配置加载器；读取失败或转换失败均保留上次有效配置。 */
class CfgLoader {
   public:
    /**
     * @brief 注入新配置仓储并读取配置。
     * @param[in] database 进程内公共数据库上下文，必须提供 device_config。
     * @param[in] ctx 读取期限。
     * @return 是否成功；失败原因通过 last_error 返回。
     */
    bool initialize(ems::database::MySqlDatabaseContext database,
                    const ems::core::CallContext& ctx);
    /**
     * @brief 在同一事务读取新表并构建运行配置，失败保留原配置。
     * @param[in] ctx 读取期限。
     * @return 是否成功。
     */
    bool reload(const ems::core::CallContext& ctx);
    /**
     * @brief 从新表记录编译运行目录；不访问数据库，用于离线校验与测试。
     * @param[in] records 新设备、通道、绑定及四遥记录。
     * @return 是否成功；失败不改变已加载目录。
     */
    bool configure(const ems::database::DeviceConfigRecords& records);

    /** @brief 返回启用且有有效绑定的通道。 @return 通道配置副本。 */
    std::vector<ems::rtdb::ChannelConfig> channels() const { return channels_; }

    /** @brief 返回授权启用的 IEC61850 上送通道。 @return 通道及已校验映射的副本。 */
    std::vector<ForwardingChannelConfig> forwarding_channels() const { return forwarding_; }

    /**
     * @brief 返回指定通道的设备与测点路由。
     * @param[in] channel_id cfg_channel 数字主键。
     * @return 设备上下文副本；无有效点时为空。
     */
    std::vector<ConfiguredDevice> devices(std::int64_t channel_id) const;

    /**
     * @brief 返回通道绑定所属的实体设备子树根，station/unit 组织节点不作为复用边界。
     * @param[in] channel_id cfg_channel 数字主键。
     * @return 同一连续 device 子树的根 device_id；未知通道或跨实体设备绑定返回空串，禁止自动复用。
     */
    std::string connection_scope(std::int64_t channel_id) const;

    /** @brief 查询初始化状态。 @return 是否已有有效配置。 */
    bool is_initialized() const { return initialized_; }

    /** @brief 返回站根修订号。 @return 已加载修订号。 */
    std::int64_t config_revision() const { return revision_; }

    /** @brief 返回上次错误。 @return 脱敏配置错误。 */
    const std::string& last_error() const { return error_; }

   private:
    ems::database::MySqlDatabaseContext database_;
    std::vector<ems::rtdb::ChannelConfig> channels_;
    std::vector<ForwardingChannelConfig> forwarding_;
    std::map<std::int64_t, std::vector<ConfiguredDevice>> devices_;
    std::map<std::int64_t, std::string> connection_scopes_;
    std::int64_t revision_{0};
    bool initialized_{false};
    std::string error_;
};
}  // namespace ems::net::device
