#pragma once

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

class ITimeSeriesDatabase : public ITimeSeriesStore, public ITimeSeriesReader {
   public:
    /** @brief 销毁可读写时序库接口。 */
    ~ITimeSeriesDatabase() override = default;
};

}  // namespace ems::database
