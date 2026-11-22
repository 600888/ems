#pragma once

#include <common/database/i_transaction.h>
#include <common/model/alarm.h>

#include <optional>
#include <variant>
#include <vector>

namespace ems::database {

/// TDengine 与 InfluxDB 共用的告警时间上限，按非负 Unix 微秒校验。
constexpr core::UtcMicros kMaxAlarmArchiveTimeUs = 9223372036854775LL;
constexpr std::uint32_t kMaxActiveAlarmRows = 20000;
constexpr std::uint32_t kMaxPendingAlarmRows = 2048;

struct AlarmOccurrence {
    std::string active_key;  ///< 站内稳定的设备、规则与条件实例键。
    std::string alarm_code;
    std::string name;
    model::AlarmSeverity severity{model::AlarmSeverity::Warning};
    std::string device_name;
    std::string device_path;
    std::string trigger_summary;
    std::string point_id;
    std::string point_name;
    std::string trigger_condition;
    std::string recovery_condition;
    std::string trigger_value;
};

struct AlarmRecovery {};

struct AlarmAcknowledgement {
    std::string user_id;
    std::string display_name;
    std::string remark;  ///< 由调用方校验并去除首尾空白。
};

struct AlarmChange {
    core::EventId event_id;
    model::AlarmId alarm_id;
    core::StationId station_id;
    core::DeviceId device_id;
    core::UtcMicros event_time_us{core::kInvalidUtcMicros};
    std::string detail;
    std::variant<AlarmOccurrence, AlarmRecovery, AlarmAcknowledgement> change;
};

struct AlarmState {
    model::AlarmId alarm_id;
    core::StationId station_id;
    core::DeviceId device_id;
    std::optional<std::string> active_key;
    core::UtcMicros occurred_at_us{0};
    std::optional<core::UtcMicros> cleared_at_us;
    std::optional<core::UtcMicros> acknowledged_at_us;
    std::string state_json;  ///< 完整当前状态，由仓储编码。
};

enum class AlarmChangeResult { Applied, Duplicate, AlreadyAcknowledged };

struct AlarmArchiveProgress {
    std::int64_t committed_seq{0};
    std::int64_t archived_seq{0};
};

struct AlarmArchiveItem {
    core::EventId event_id;
    model::AlarmId alarm_id;
    core::StationId station_id;
    core::DeviceId device_id;
    model::AlarmLifecycle kind{model::AlarmLifecycle::Occurred};
    core::UtcMicros event_time_us{0};
    std::int64_t stream_seq{0};
    std::string event_json;  ///< 不可变事件，供时序适配器写入。
    std::string state_json;  ///< 该事件提交后的完整状态。
};

class IAlarmRepository {
   public:
    /** @brief 释放仓储。 */
    virtual ~IAlarmRepository() = default;

    /**
     * @brief 暂存状态变更、连续事件序号与 outbox。调用方负责校验设备归属及操作权限。
     * @param[in,out] tx 调用方事务。首次确认审计由仓储在同一事务追加。
     * @param[in] change 可信来源提供的事件。重试须保留 ID、事实时间与载荷。
     * @param[in] ctx 调用期限。
     * @return 新变更、同事件重试或已确认；同 ID 异载荷返回 Conflict。
     */
    virtual core::Result<AlarmChangeResult> apply(ITransaction& tx, const AlarmChange& change,
                                                  const core::CallContext& ctx) = 0;

    /**
     * @brief 按站和实例读取当前状态。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 已核验的站 ID。
     * @param[in] alarm_id 稳定实例 ID。
     * @param[in] ctx 调用期限。
     * @return 可空状态或错误。
     */
    virtual core::Result<std::optional<AlarmState>> find(ITransaction& tx,
                                                         const core::StationId& station_id,
                                                         const model::AlarmId& alarm_id,
                                                         const core::CallContext& ctx) = 0;

    /**
     * @brief 按实例 ID 游标读取至多 128 个活动实例，供生产者重启时恢复身份。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 已核验的站 ID。
     * @param[in] after 上一页最后一个实例 ID；空 ID 从头开始。
     * @param[in] ctx 调用期限。
     * @return 当前页或错误。
     */
    virtual core::Result<std::vector<AlarmState>> active(ITransaction& tx,
                                                         const core::StationId& station_id,
                                                         const model::AlarmId& after,
                                                         const core::CallContext& ctx) = 0;

    /**
     * @brief 以单次读取获取同站活动告警，供一致的列表和计数使用。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 已核验的站 ID。
     * @param[in] ctx 调用期限。
     * @return 活动告警；超出预算时返回 ResourceExhausted。
     */
    virtual core::Result<std::vector<AlarmState>> active_snapshot(ITransaction& tx,
                                                                  const core::StationId& station_id,
                                                                  const core::CallContext& ctx) = 0;

    /**
     * @brief 读取站的归档边界。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 稳定站 ID。
     * @param[in] ctx 调用期限。
     * @return 当前边界；首次提交之前两个边界均为 0。
     */
    virtual core::Result<AlarmArchiveProgress> progress(ITransaction& tx,
                                                        const core::StationId& station_id,
                                                        const core::CallContext& ctx) = 0;

    /**
     * @brief 读取固定边界内的未归档事件，含读取时已推进归档的原始行。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 已核验的站 ID。
     * @param[in] bounds 读取时固定的已归档和已提交位置。
     * @param[in] alarm_id 详情查询的实例 ID；空 ID 读取完整尾部。
     * @param[in] ctx 调用期限。
     * @return 事件及完整状态，按站内事件顺序返回；超出预算时返回错误。
     */
    virtual core::Result<std::vector<AlarmArchiveItem>> pending(ITransaction& tx,
                                                                const core::StationId& station_id,
                                                                const AlarmArchiveProgress& bounds,
                                                                const model::AlarmId& alarm_id,
                                                                const core::CallContext& ctx) = 0;

    /**
     * @brief 读取尚未归档的首个事件。结束读取事务后才可写时序库。
     * @param[in,out] tx 调用方事务。
     * @param[in] station_id 稳定站 ID。
     * @param[in] ctx 调用期限。
     * @return 可空事件或错误。重读同一位置返回相同载荷。
     */
    virtual core::Result<std::optional<AlarmArchiveItem>> next_for_archive(
        ITransaction& tx, const core::StationId& station_id, const core::CallContext& ctx) = 0;

    /**
     * @brief 在事件与状态都已写入时序库后登记完成，不能跳过归档位置。
     * @param[in,out] tx 调用方事务。
     * @param[in] item 原始归档项，用于校验站、序号与事件 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果；相同归档项重复登记幂等，序号跳跃或身份不匹配返回 Conflict。
     */
    virtual core::Status mark_archived(ITransaction& tx, const AlarmArchiveItem& item,
                                       const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
