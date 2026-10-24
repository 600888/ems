/**
 * @file realtime_service.h
 * @brief 类型化实时业务门面：测点样本、设备/通道状态、版本一致性与能力运行条件。
 *
 * 本门面位于 IRealtimeStore 之上，负责 access-design.md 第 5.2/5.3 节
 * 规定的业务规则：稳定点地址解析、来源授权核对、序号新旧判断、新鲜度
 * 计算、完整字段替换与 CAS 重试；不直接暴露 Redis/共享内存键。
 *
 * 边界：
 * - 只做运行条件判断，不代替策略仲裁、用户权限或设备控制权选主。
 * - 不做设备 I/O；命令执行仍由采集/控制模块完成。
 * - 配置版本由 RuntimeCatalog 固定注入，门面不会自行读取“最新”配置。
 */
#pragma once

#include <common/core/call_context.h>
#include <common/core/identity.h>
#include <common/core/result.h>
#include <common/core/time.h>
#include <common/model/point.h>
#include <rtdb/export.h>
#include <rtdb/model/runtime_catalog.h>
#include <rtdb/store/realtime_store.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ems::rtdb {

/// 测点写入的业务结果；与底层 ErrorCode 分开，避免把“旧样本”当成失败。
enum class SampleResult {
    Applied,           ///< 已写入并产生新 revision
    Duplicate,         ///< 相同序号、相同载荷，幂等重复
    IgnoredOutOfOrder  ///< 序号或来源代次落后，未修改最新值
};

/// 一次测点更新的输入。
struct PointUpdate {
    PointAddress address;
    model::PointSample sample;
};

/// 类型化测点快照。
struct PointSnapshot {
    core::PointId point_id;                                 ///< 稳定点 ID
    model::PointType point_type{model::PointType::YC};      ///< 四遥分类
    model::PointKind kind{model::PointKind::Telemetry};     ///< 点种类
    model::ValueType value_type{model::ValueType::Double};  ///< 值类型
    std::string unit;                                       ///< 工程单位
    std::optional<model::PointValue> value;                 ///< 无值时为 nullopt
    model::QualityInfo quality;                             ///< 质量码与原因
    bool stale{false};                                      ///< 超过配置时效阈值
    bool clock_anomaly{false};                              ///< 源时间明显超前本地时钟
    bool time_degraded{false};                              ///< 源时间缺失，使用接收时间判定
    bool config_current{true};                              ///< 样本版本等于目录版本
    bool source_authorized{true};                           ///< 样本来源等于已安装授权
    bool writable{false};                                   ///< 当前授权是否允许采样写入
    core::Sequence sequence{0};
    core::SourceEpoch source_epoch{};
    std::string source_id;
    std::string config_version;
    core::UtcMicros source_time{core::kInvalidUtcMicros};
    core::UtcMicros ingest_time{core::kInvalidUtcMicros};
    std::string event_id;
    Revision revision;  ///< 读取时的存储版本
};

/// 设备运行状态（与设计图“在线/告警/故障/离线/未配置”一致）。
enum class DeviceRunState { Unconfigured, Online, Alarm, Fault, Offline };

/**
 * @brief 返回设备运行状态的稳定文本（小写，用于实时记录与接口）。
 * @param[in] state 运行状态。
 * @return 稳定文本。
 */
EMS_RTDB_API const char* to_string(DeviceRunState state) noexcept;

/**
 * @brief 解析设备运行状态文本。
 * @param[in] text 小写状态文本。
 * @return 解析结果；未知文本为空。
 */
EMS_RTDB_API std::optional<DeviceRunState> parse_device_state(const std::string& text);

/// 设备状态样本（采集侧唯一写角色）。
struct DeviceStateSample {
    DeviceRunState state{DeviceRunState::Unconfigured};
    model::QualityInfo quality;
    core::UtcMicros since{core::kInvalidUtcMicros};            ///< 进入当前状态的时间
    core::UtcMicros last_valid_time{core::kInvalidUtcMicros};  ///< 最近有效数据时间
    std::uint32_t active_alarms{0};
    std::string config_version;
    std::string source_id;
    core::SourceEpoch source_epoch{};
    core::Sequence sequence{0};
};

/// 设备状态快照。
struct DeviceSnapshot {
    core::DeviceId device_id;
    DeviceStateSample sample;
    bool stale{false};  ///< 最近有效数据超过设备状态时效阈值
    bool config_current{true};
    Revision revision;
};

/// 通道运行状态。
enum class ChannelRunState { Disconnected, Connected, Degraded };

/**
 * @brief 返回通道运行状态的稳定文本。
 * @param[in] state 运行状态。
 * @return 稳定文本。
 */
EMS_RTDB_API const char* to_string(ChannelRunState state) noexcept;

/**
 * @brief 解析通道运行状态文本。
 * @param[in] text 小写状态文本。
 * @return 解析结果；未知文本为空。
 */
EMS_RTDB_API std::optional<ChannelRunState> parse_channel_state(const std::string& text);

/// 通道状态样本；统计字段表示当前窗口累计值。
struct ChannelStateSample {
    ChannelRunState state{ChannelRunState::Disconnected};
    std::string reason;
    core::UtcMicros since{core::kInvalidUtcMicros};
    core::UtcMicros window_start{core::kInvalidUtcMicros};
    core::UtcMicros last_success_time{core::kInvalidUtcMicros};
    std::string last_error;
    std::uint64_t requests{0};
    std::uint64_t successes{0};
    std::uint64_t timeouts{0};
    double average_latency_ms{0.0};
    double max_latency_ms{0.0};
    std::string config_version;
    std::string source_id;
    core::SourceEpoch source_epoch{};
    core::Sequence sequence{0};
};

/// 通道状态快照，附带由计数计算的派生值。
struct ChannelSnapshot {
    core::ChannelId channel_id;
    ChannelStateSample sample;
    double success_rate{0.0};  ///< successes / requests；无请求时为 0
    Revision revision;
};

/// 一次通信窗口增量；用于 5 分钟统计的读改写。
struct ChannelHealthDelta {
    ChannelRunState state{ChannelRunState::Connected};
    std::string reason;
    core::UtcMicros observed_at{core::kInvalidUtcMicros};
    std::uint64_t requests{0};
    std::uint64_t successes{0};
    std::uint64_t timeouts{0};
    double average_latency_ms{0.0};
    double max_latency_ms{0.0};
    std::string last_error;
    std::string config_version;
    std::string source_id;
    core::SourceEpoch source_epoch{};
    core::Sequence sequence{0};
};

/// 作用域运行版本记录（实时库根的 DeviceState 记录）。
struct ScopeRuntime {
    std::string config_version;
    std::string activation_revision;
    core::UtcMicros applied_at{core::kInvalidUtcMicros};
};

/// 能力所需单个点的检查结果。
struct CapabilityPointCheck {
    core::PointId point_id;
    std::string code;
    bool ok{false};
    std::string reason;  ///< 稳定原因码；ok 时为空
};

/// 能力运行条件结果；不包含权限与策略仲裁结论。
struct CapabilityAvailability {
    core::DeviceId device_id;
    std::string capability_code;
    bool available{false};
    std::vector<std::string> reasons;          ///< 去重后的稳定原因码
    std::vector<CapabilityPointCheck> points;  ///< 逐点诊断
};

/**
 * @brief 类型化实时业务门面。
 *
 * 实例不持有可变状态，可被多个线程并发调用；存储对象的生命周期由进程
 * 组装者保证（停止调用 → 等待在途 → close → 析构）。目录对象不可变。
 */
class EMS_RTDB_API RealtimeService {
   public:
    /// 时钟注入：默认使用系统 UTC 微秒；测试可固定时间验证陈旧判断。
    using Clock = std::function<core::UtcMicros()>;

    /**
     * @brief 构造门面。
     * @param[in] store 已打开的统一实时存储。
     * @param[in] catalog 已发布的运行目录；不能为空。
     * @param[in] clock 可选时钟；为空时使用 core::now_utc_micros。
     */
    RealtimeService(std::shared_ptr<IRealtimeStore> store,
                    std::shared_ptr<const RuntimeCatalog> catalog, Clock clock = {});

    /**
     * @brief 读取单个测点的类型化快照。
     * @param[in] address 测点地址。
     * @param[in] ctx 调用预算。
     * @return 快照；目录无此点或记录不存在返回 NotFound。
     */
    core::Result<PointSnapshot> getPoint(const PointAddress& address, const core::CallContext& ctx);

    /**
     * @brief 批量读取测点；逐项结果与输入等长同序。
     * @param[in] addresses 测点地址列表。
     * @param[in] ctx 整次调用共用的预算。
     * @return 逐项快照或错误；不保证多点同一时刻快照。
     */
    std::vector<core::Result<PointSnapshot>> getPoints(const std::vector<PointAddress>& addresses,
                                                       const core::CallContext& ctx);

    /**
     * @brief 写入一次测点采样。
     *
     * 规则：点必须启用且记录中已安装授权允许写入；source_id/epoch/
     * config_version 必须等于记录 accepted_*；同代次序号更小返回
     * IgnoredOutOfOrder，相同序号相同载荷返回 Duplicate，相同序号不同
     * 载荷返回 Conflict；CAS 冲突有限重试，超过预算返回 DeadlineExceeded。
     *
     * @param[in] address 测点地址。
     * @param[in] sample 类型化采样。
     * @param[in] ctx 调用预算。
     * @return 业务结果或结构化错误。
     */
    core::Result<SampleResult> updatePoint(const PointAddress& address,
                                           const model::PointSample& sample,
                                           const core::CallContext& ctx);

    /**
     * @brief 批量写入采样；逐项独立，不整批回滚。
     * @param[in] updates 采样列表。
     * @param[in] ctx 整次调用共用的预算。
     * @return 与输入等长同序的业务结果。
     */
    std::vector<core::Result<SampleResult>> updatePoints(const std::vector<PointUpdate>& updates,
                                                         const core::CallContext& ctx);

    /**
     * @brief 为首次启用的点创建无值记录。
     * @param[in] address 测点地址。
     * @param[in] ctx 调用预算。
     * @return 新记录 revision；已存在返回 Conflict。
     */
    core::Result<Revision> createPoint(const PointAddress& address, const core::CallContext& ctx);

    /**
     * @brief 安装/切换测点来源授权，保留最后样本。
     * @param[in] address 测点地址。
     * @param[in] policy 新授权；写权限随之切换。
     * @param[in] ctx 调用预算。
     * @return 新 revision；记录不存在时按新授权创建。
     */
    core::Result<Revision> authorizePoint(const PointAddress& address, const SourcePolicy& policy,
                                          const core::CallContext& ctx);

    /**
     * @brief 读取设备运行状态。
     * @param[in] device_id 设备 ID。
     * @param[in] ctx 调用预算。
     * @return 状态快照；设备未配置或尚未上报返回 NotFound。
     */
    core::Result<DeviceSnapshot> getDeviceState(const core::DeviceId& device_id,
                                                const core::CallContext& ctx);

    /**
     * @brief 写入设备运行状态（采集侧唯一写角色）。
     * @param[in] device_id 设备 ID。
     * @param[in] sample 状态样本。
     * @param[in] ctx 调用预算。
     * @return 新 revision；旧代次/旧序号返回 Conflict 或 IgnoredOutOfOrder 语义错误。
     */
    core::Result<Revision> updateDeviceState(const core::DeviceId& device_id,
                                             const DeviceStateSample& sample,
                                             const core::CallContext& ctx);

    /**
     * @brief 读取通道运行状态与派生成功率。
     * @param[in] channel_id 通道 ID。
     * @param[in] ctx 调用预算。
     * @return 通道快照；未配置或未上报返回 NotFound。
     */
    core::Result<ChannelSnapshot> getChannelState(const core::ChannelId& channel_id,
                                                  const core::CallContext& ctx);

    /**
     * @brief 覆盖写入通道状态（用于状态切换与窗口重置）。
     * @param[in] channel_id 通道 ID。
     * @param[in] sample 完整状态样本。
     * @param[in] ctx 调用预算。
     * @return 新 revision。
     */
    core::Result<Revision> updateChannelState(const core::ChannelId& channel_id,
                                              const ChannelStateSample& sample,
                                              const core::CallContext& ctx);

    /**
     * @brief 累加一次通信窗口增量并返回最新快照。
     *
     * 距 `window_start` 满 5 分钟时重置计数；平均时延按请求数加权。
     * 旧序号增量返回 Conflict 且不修改记录，调用方可重读后重报。
     *
     * @param[in] channel_id 通道 ID。
     * @param[in] delta 本次窗口增量。
     * @param[in] ctx 调用预算。
     * @return 累加后的通道快照。
     */
    core::Result<ChannelSnapshot> reportChannelHealth(const core::ChannelId& channel_id,
                                                      const ChannelHealthDelta& delta,
                                                      const core::CallContext& ctx);

    /**
     * @brief 读取作用域运行版本记录。
     * @param[in] ctx 调用预算。
     * @return 作用域运行版本；尚未应用返回 NotFound。
     */
    core::Result<ScopeRuntime> getScopeRuntime(const core::CallContext& ctx);

    /**
     * @brief 条件写入作用域运行版本。
     * @param[in] runtime 新版本与激活修订号。
     * @param[in] expected 期望的当前 revision；nullopt 表示仅创建。
     * @param[in] ctx 调用预算。
     * @return 新 revision；版本不符返回 Conflict。
     */
    core::Result<Revision> installScopeRuntime(const ScopeRuntime& runtime,
                                               const std::optional<Revision>& expected,
                                               const core::CallContext& ctx);

    /**
     * @brief 按当前运行条件解析设备能力可用性。
     *
     * 逐点检查配置启用、是否有样本、值是否存在、质量、新鲜度、配置版本
     * 与来源授权。结果不包含用户权限或控制权裁决，调用方必须另行授权。
     *
     * @param[in] device_id 设备 ID。
     * @param[in] capability_code 能力编码。
     * @param[in] ctx 调用预算。
     * @return 可用性及逐点原因；设备未配置返回 NotFound。
     */
    core::Result<CapabilityAvailability> resolveCapability(const core::DeviceId& device_id,
                                                           const std::string& capability_code,
                                                           const core::CallContext& ctx);

    /**
     * @brief 返回注入的运行目录。
     * @return 目录引用。
     */
    const RuntimeCatalog& catalog() const noexcept { return *catalog_; }

   private:
    std::shared_ptr<IRealtimeStore> store_;
    std::shared_ptr<const RuntimeCatalog> catalog_;
    Clock clock_;
};

}  // namespace ems::rtdb
