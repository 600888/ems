/**
 * @file runtime_catalog.h
 * @brief 已发布配置的运行目录：稳定点地址、设备/通道路径、来源授权与能力依赖。
 *
 * 运行目录是配置与实时库之间的只读边界：实时库不查询 MySQL，也不解析
 * 点表文件，只接收配置服务从设备树配置（cfg_device + 四遥点表）投影出的
 * 设备树快照，并使用本目录提供的稳定身份、类型、时效阈值与授权策略。
 * 目录构建完成后只读，可被多个线程共享。
 *
 * 与数据库的对应关系见 website/docs/common/database/device-management.md
 * 第 10 节：cfg_device 设备树 → PointAddress.device_path，
 * cfg_device.config_revision → RuntimeCatalog::configVersion()（文本如 r37）。
 */
#pragma once

#include <common/core/identity.h>
#include <common/model/point.h>
#include <rtdb/export.h>
#include <rtdb/store/realtime_store.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ems::rtdb {

/// 设备树节点种类（与 cfg_device.node_kind 一致）
enum class NodeKind : std::uint8_t {
    Station = 0,   ///< 站根
    Unit = 1,      ///< 储能单元
    Device = 2,    ///< 通信设备
    Component = 3  ///< 部件（电池簇/包/电芯，可有点但无独立协议连接）
};

/**
 * @brief 标准点在实时库中的逻辑地址。
 *
 * 路径由设备树相对作用域根解析，编码规则与 RecordKey 的 device_path 一致：
 * 最多 8 级、每级为稳定业务编码。空路径表示作用域根（仅用于作用域运行
 * 记录），普通设备点至少包含设备节点一级。
 */
struct PointAddress {
    Scope scope;                           ///< 单元或站作用域
    std::vector<std::string> device_path;  ///< 设备路径（不含作用域根）
    std::string code;                      ///< 点业务编码，同一节点内唯一
};

/**
 * @brief 判断两个测点地址是否完全相同。
 * @param[in] lhs 左侧地址。
 * @param[in] rhs 右侧地址。
 * @return true 表示作用域、路径与编码都相同。
 */
inline bool operator==(const PointAddress& lhs, const PointAddress& rhs) {
    return lhs.scope.kind == rhs.scope.kind && lhs.scope.code == rhs.scope.code &&
           lhs.device_path == rhs.device_path && lhs.code == rhs.code;
}

/**
 * @brief 判断两个测点地址是否不同。
 * @param[in] lhs 左侧地址。
 * @param[in] rhs 右侧地址。
 * @return true 表示至少一个字段不同。
 */
inline bool operator!=(const PointAddress& lhs, const PointAddress& rhs) { return !(lhs == rhs); }

/**
 * @brief 来源授权模板（首次创建或授权切换时安装到实时记录）。
 *
 * 写入判定以记录中的 accepted_source_id / epoch / config_version 为准，
 * 目录中的副本只作为 createPoint 的初始值。普通采样不能通过传入更大的
 * epoch 自行取得权限，授权切换必须由配置/授权流程调用 authorizePoint。
 */
struct SourcePolicy {
    std::string source_id;       ///< 已批准来源标识
    core::SourceEpoch epoch{};   ///< 来源授权代次
    std::string config_version;  ///< 产生该授权时所应用的配置版本
    bool write_enabled{false};   ///< 是否允许采样写入
};

/// 运行目录中的标准点定义。
///
/// point_type 是调度语义（遥测/遥信/遥控/遥调），kind 是实时记录的存储种类。
/// 遥控与遥调都存为 Command，控制侧必须读 point_type 才能区分“固定值写入”
/// 与“工程值缩放写入”，不能只看 kind。
struct RuntimePoint {
    core::PointId point_id;    ///< 稳定点 ID
    core::DeviceId device_id;  ///< 所属设备树节点（设备或部件）；单元/站根点为空
    PointAddress address;      ///< 实时记录地址
    model::PointType point_type{model::PointType::YC};      ///< 四遥分类
    model::PointKind kind{model::PointKind::Telemetry};     ///< 记录种类
    model::ValueType value_type{model::ValueType::Double};  ///< 值类型
    std::string unit;                                       ///< 工程单位
    std::uint32_t stale_after_ms{60000};                    ///< 陈旧阈值
    bool enabled{true};                                     ///< 配置启用
    SourcePolicy source;                                    ///< 已安装的来源授权
};

/// 运行目录中的设备：用于解析设备状态记录路径。
struct RuntimeDevice {
    core::DeviceId device_id;
    std::string code;                      ///< 设备编码
    Scope scope;                           ///< 设备状态记录所属作用域
    std::vector<std::string> device_path;  ///< 设备路径，不能为空
    bool enabled{true};
};

/// 运行目录中的通道：通道状态记录固定使用站作用域。
struct RuntimeChannel {
    core::ChannelId channel_id;
    std::string code;
    Scope scope;  ///< 站作用域
    bool enabled{true};
};

/// 能力依赖：设备 + 能力编码 → 需要的标准点与反馈点。
struct RuntimeCapability {
    core::DeviceId device_id;
    std::string capability_code;
    std::vector<core::PointId> required_points;
    std::vector<core::PointId> required_feedbacks;
};

/// 运行目录构建参数。
struct RuntimeCatalogOptions {
    Scope scope;                                  ///< 目录对应作用域；根节点按 code 匹配
    std::string config_version;                   ///< 必填，激活清单中的精确版本
    std::string activation_revision;              ///< 可选，激活修订号
    std::uint32_t default_stale_after_ms{60000};  ///< 点未单独配置时使用
    SourcePolicy default_source;                  ///< 初始来源授权，可后续按点覆盖
};

/**
 * @brief 只读运行目录。
 *
 * 构建期通过 addPoint/addDevice/addChannel/addCapability 填充，查询期只读。
 * 重复的稳定 ID、地址或能力键被拒绝而不是覆盖，避免静默丢失配置。
 */
class EMS_RTDB_API RuntimeCatalog {
   public:
    /** @brief 构造空目录（仅用于反序列化或占位，运行前必须填充）。 */
    RuntimeCatalog() = default;

    /**
     * @brief 构造带作用域与版本信息的目录。
     * @param[in] scope 作用域。
     * @param[in] config_version 精确配置版本。
     * @param[in] activation_revision 激活修订号，可为空。
     * @param[in] default_stale_after_ms 默认陈旧阈值。
     */
    RuntimeCatalog(Scope scope, std::string config_version, std::string activation_revision,
                   std::uint32_t default_stale_after_ms);

    /**
     * @brief 添加一个标准点定义。
     * @param[in] point 已解析路径与来源授权的点。
     * @return true 表示添加成功；地址或点 ID 重复返回 false。
     */
    bool addPoint(RuntimePoint point);

    /**
     * @brief 添加设备运行定义。
     * @param[in] device 设备及其状态记录路径。
     * @return true 表示添加成功；设备 ID 重复或路径为空返回 false。
     */
    bool addDevice(RuntimeDevice device);

    /**
     * @brief 添加通道运行定义。
     * @param[in] channel 通道及站作用域。
     * @return true 表示添加成功；通道 ID 重复返回 false。
     */
    bool addChannel(RuntimeChannel channel);

    /**
     * @brief 添加能力依赖。
     * @param[in] capability 设备能力及其所需点。
     * @return true 表示添加成功；同一设备能力重复返回 false。
     */
    bool addCapability(RuntimeCapability capability);

    /**
     * @brief 按逻辑地址查找点定义。
     * @param[in] address 测点地址。
     * @return 匹配的定义；未找到时为空。
     */
    const RuntimePoint* findPoint(const PointAddress& address) const;

    /**
     * @brief 按稳定点 ID 查找点定义。
     * @param[in] point_id 稳定点 ID。
     * @return 匹配的定义；未找到时为空。
     */
    const RuntimePoint* findPoint(const core::PointId& point_id) const;

    /**
     * @brief 按稳定设备 ID 查找设备定义。
     * @param[in] device_id 设备 ID。
     * @return 匹配的定义；未找到时为空。
     */
    const RuntimeDevice* findDevice(const core::DeviceId& device_id) const;

    /**
     * @brief 按稳定通道 ID 查找通道定义。
     * @param[in] channel_id 通道 ID。
     * @return 匹配的定义；未找到时为空。
     */
    const RuntimeChannel* findChannel(const core::ChannelId& channel_id) const;

    /**
     * @brief 查找设备能力依赖。
     * @param[in] device_id 设备 ID。
     * @param[in] capability_code 能力编码。
     * @return 匹配的定义；未配置时为空。
     */
    const RuntimeCapability* findCapability(const core::DeviceId& device_id,
                                            const std::string& capability_code) const;

    /**
     * @brief 返回目录作用域。
     * @return 作用域引用。
     */
    const Scope& scope() const noexcept { return scope_; }

    /**
     * @brief 返回目录对应的精确配置版本。
     * @return 配置版本文本。
     */
    const std::string& configVersion() const noexcept { return config_version_; }

    /**
     * @brief 返回激活修订号（可为空）。
     * @return 激活修订号文本。
     */
    const std::string& activationRevision() const noexcept { return activation_revision_; }

    /**
     * @brief 返回设备状态默认陈旧阈值。
     * @return 毫秒数。
     */
    std::uint32_t defaultStaleAfterMs() const noexcept { return default_stale_after_ms_; }

    /**
     * @brief 返回已加载的点数量。
     * @return 点数量。
     */
    std::size_t pointCount() const noexcept { return points_.size(); }

    /**
     * @brief 返回已加载的设备数量。
     * @return 设备数量。
     */
    std::size_t deviceCount() const noexcept { return devices_.size(); }

    /**
     * @brief 返回已加载的通道数量。
     * @return 通道数量。
     */
    std::size_t channelCount() const noexcept { return channels_.size(); }

   private:
    Scope scope_;
    std::string config_version_;
    std::string activation_revision_;
    std::uint32_t default_stale_after_ms_{60000};

    std::vector<RuntimePoint> points_;
    std::vector<RuntimeDevice> devices_;
    std::vector<RuntimeChannel> channels_;
    std::vector<RuntimeCapability> capabilities_;

    std::unordered_map<std::string, std::size_t> point_address_index_;
    std::unordered_map<std::string, std::size_t> point_id_index_;
    std::unordered_map<std::string, std::size_t> device_index_;
    std::unordered_map<std::string, std::size_t> channel_index_;
    std::unordered_map<std::string, std::size_t> capability_index_;
};

/// 构建结果；issues 非空时目录只包含可解析部分，不得用于发布/运行。
struct RuntimeCatalogBuildResult {
    RuntimeCatalogOptions options;  ///< 回显构建参数，便于诊断
    std::size_t point_count{0};
    std::vector<std::string> issues;  ///< 逐项构建问题
    RuntimeCatalog catalog;           ///< 已构建目录（可能为空）

    /**
     * @brief 判断目录是否完整构建。
     * @return true 表示没有任何构建问题。
     */
    bool ok() const noexcept { return issues.empty(); }
};

/// 运行目录构建输入：设备树节点（配置服务从 cfg_device 行投影，实时库不感知数据库表）。
struct CatalogNode {
    core::DeviceId node_id;      ///< 稳定节点 ID（设备与部件共用 cfg_device.device_id 空间）
    std::string parent_node_id;  ///< 父节点 ID；站根为空
    NodeKind kind{NodeKind::Device};
    std::string code;  ///< 同父节点下唯一的业务编码
    bool enabled{true};
};

/// 运行目录构建输入：四遥点（配置服务从 cfg_point_yc/yx/yk/yt 行投影）。
struct CatalogPoint {
    core::PointId point_id;
    std::string owner_node_id;                           ///< 点归属节点；单元/站根点填根节点 ID
    std::string code;                                    ///< 同一归属节点下唯一的业务编码
    model::PointType point_type{model::PointType::YC};   ///< 四遥分类
    model::PointKind kind{model::PointKind::Telemetry};  ///< 记录种类；必须与 point_type 自洽
    model::ValueType value_type{model::ValueType::Double};
    std::string unit;
    std::uint32_t stale_after_ms{0};  ///< 0 表示使用构建参数中的默认值
    bool enabled{true};
};

/// 运行目录构建输入：通道（通道状态记录固定使用站作用域）。
struct CatalogChannel {
    core::ChannelId channel_id;
    std::string code;
    bool enabled{true};
};

/// 一次运行目录构建的输入快照：由配置服务在内存中投影自设备管理配置。
struct DeviceTreeSnapshot {
    std::vector<CatalogNode> nodes;
    std::vector<CatalogPoint> points;
    std::vector<CatalogChannel> channels;
};

/**
 * @brief 由设备树快照编译运行目录。
 *
 * 只做内存转换，不访问数据库。作用域根按 options.scope 的 kind 与 code
 * 在设备树中匹配（Station↔站根、Unit↔单元）；根之下的节点路径写入
 * PointAddress.device_path，作用域根之外的节点跳过。设备树成环、父节点
 * 缺失、路径超过 8 级、编码非法、点归属缺失等问题写入 issues，由调用方
 * 拒绝发布，而不是部分生效。
 *
 * @param[in] snapshot 配置服务投影出的设备树快照。
 * @param[in] options 作用域、版本与来源授权。
 * @return 运行目录及逐项构建问题。
 */
EMS_RTDB_API RuntimeCatalogBuildResult buildRuntimeCatalog(const DeviceTreeSnapshot& snapshot,
                                                           const RuntimeCatalogOptions& options);

}  // namespace ems::rtdb
