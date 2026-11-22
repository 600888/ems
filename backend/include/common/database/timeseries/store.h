#pragma once

#include <common/database/alarm/repositories.h>
#include <common/database/timeseries/types.h>

namespace ems::database {

class ITimeSeriesStore {
   public:
    /** @brief 销毁时序写入接口。 */
    virtual ~ITimeSeriesStore() = default;

    /**
     * @brief 写入类型化样本，行身份为点位、原始微秒时间和事件 ID。
     * @param[in] sample 经目录及账本核验的样本。
     * @param[in] ctx 调用截止时间。
     * @return 同身份同载荷重投须幂等；确认提交后成功，未知结果返回错误。
     */
    virtual core::Status write(const Sample& sample, const core::CallContext& ctx) = 0;

    /**
     * @brief 有界批量写入，按输入顺序报告每条样本的确定结果。
     * @param[in] samples 同一次实际采样产生的记录，原生后端每批最多 256 条。
     * @param[in] ctx 整批调用期限；未执行项返回 DeadlineExceeded，不伪装成成功。
     * @return 与输入等长的状态数组，部分失败和 Unknown 保留到对应记录。
     */
    virtual std::vector<core::Status> write_batch(const std::vector<Sample>& samples,
                                                  const core::CallContext& ctx) {
        std::vector<core::Status> result;
        result.reserve(samples.size());
        for (const auto& sample : samples) {
            if (core::has_expired(ctx))
                result.emplace_back(core::make_error(core::ErrorCode::DeadlineExceeded,
                                                     core::WriteEffect::NotApplied));
            else
                result.push_back(write(sample, ctx));
        }
        return result;
    }
};

class ITimeSeriesReader {
   public:
    /** @brief 销毁时序读取接口。 */
    virtual ~ITimeSeriesReader() = default;

    /**
     * @brief 有界读取按 (ts, point_id, event_id) 严格排序的原始样本。
     * @param[in] query 已校验的查询条件。
     * @param[in] mappings 查询点位对应的逻辑序列键及值类型。
     * @param[in] limit 最多返回行数，包含用于判断下一页的额外一行。
     * @param[in] ctx 调用截止时间。
     * @return 样本列表，失败时不返回局部成功页。
     */
    virtual core::Result<std::vector<Sample>> scan(const HistoryQuery& query,
                                                   const std::vector<SeriesMapping>& mappings,
                                                   std::uint32_t limit,
                                                   const core::CallContext& ctx) = 0;
};

constexpr std::uint32_t kMaxArchivedAlarmRows = 30000;

struct AlarmArchiveQuery {
    core::StationId station_id;
    std::int64_t archived_seq{0};  ///< 仅可读取已确认完整写入的连续前缀。
    model::AlarmId alarm_id;       ///< 详情使用实例 ID，列表使用发生时间闭区间。
    core::UtcMicros start_us{0};
    core::UtcMicros end_us{0};
    bool events{false};  ///< true 读取生命周期，false 读取完整状态。
};

struct ArchivedAlarmRow {
    model::AlarmId alarm_id;
    core::DeviceId device_id;
    std::int64_t stream_seq{0};
    std::string payload;
};

class IAlarmTimeSeriesStore {
   public:
    /** @brief 销毁告警时序写入接口。 */
    virtual ~IAlarmTimeSeriesStore() = default;

    /**
     * @brief 检查告警表或对应数据库的访问条件，不执行建表。
     * @param[in] ctx 调用期限。
     * @return 检查结果；InfluxDB 写权限在首次实际写入时确认。
     */
    virtual core::Status check_alarm_schema(const core::CallContext& ctx) = 0;

    /**
     * @brief 按原事件时间和稳定身份写入事件及完整状态。同一项可安全重投。
     * @param[in] item 公共告警仓储读取的不可变归档项。
     * @param[in] ctx 两次写入共用的期限。
     * @return 两条记录均确认写入时成功。可能只落库一条时返回 Unknown。
     */
    virtual core::Status write_alarm(const AlarmArchiveItem& item,
                                     const core::CallContext& ctx) = 0;
};

class ITimeSeriesDatabase : public ITimeSeriesStore,
                            public ITimeSeriesReader,
                            public IAlarmTimeSeriesStore {
   public:
    /** @brief 销毁可读写时序库接口。 */
    ~ITimeSeriesDatabase() override = default;

    /**
     * @brief 从时序库读取固定归档边界内的告警快照或生命周期。
     * @param[in] query 查询范围；只允许按站和实例或发生时间读取。
     * @param[in] ctx 调用期限。
     * @return 有界结果；超出预算时返回错误，不返回部分结果。
     */
    virtual core::Result<std::vector<ArchivedAlarmRow>> read_alarms(
        const AlarmArchiveQuery& query, const core::CallContext& ctx) = 0;
};

}  // namespace ems::database
