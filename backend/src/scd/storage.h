#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/messaging/delivery.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "telemetry_feed.h"

namespace ems::storage {

enum class ValueType { Numeric, Integer, Boolean, Text };
using SampleValue = std::variant<std::monostate, double, std::int64_t, bool, std::string>;

struct PointSnapshot {
    std::string scope_kind;
    std::string scope_id;
    std::string config_version;
    std::string point_id;
    std::string asset_id;
    std::string point_code;
    std::string display_name;
    std::string unit;
    ValueType value_type{ValueType::Numeric};
    std::string series_key;
    bool enabled{false};
};

struct SeriesMapping {
    std::string point_id;
    ValueType value_type{ValueType::Numeric};
    std::string series_key;
};

struct Sample {
    std::string event_id;
    std::string point_id;
    std::string scope_kind;
    std::string scope_id;
    std::string config_version;
    std::string series_key;
    std::string source_id;
    std::string source_epoch;
    std::string sequence;
    std::string quality_reason;
    std::string time_reason;
    std::string payload_sha256;
    std::int64_t source_time_us{0};
    std::int64_t ingest_time_us{0};
    std::int32_t quality{0};
    std::uint8_t time_origin{0};
    ValueType value_type{ValueType::Numeric};
    SampleValue value;
};

struct QuarantineRecord {
    std::string stream_id;
    std::string scope;
    std::string event_id;
    std::string reason;
    std::string payload;
};

class IHistoryRepository {
   public:
    /** @brief 销毁历史仓储接口。 */
    virtual ~IHistoryRepository() = default;

    /**
     * @brief 按事件版本读取已就绪的点位快照和稳定序列映射。
     * @param[in] scope_kind 作用域类型。
     * @param[in] scope_id 作用域 ID。
     * @param[in] version 配置版本。
     * @param[in] point_id 稳定点位 ID。
     * @param[in] ctx 调用截止时间。
     * @return 点位快照；版本未就绪返回 NotReady，永久非法返回 InvalidArgument。
     */
    virtual core::Result<PointSnapshot> find_point(const std::string& scope_kind,
                                                   const std::string& scope_id,
                                                   const std::string& version,
                                                   const std::string& point_id,
                                                   const core::CallContext& ctx) = 0;

    /**
     * @brief 读取稳定点位对应的值类型和后端无关的序列键。
     * @param[in] point_id 稳定点位 ID。
     * @param[in] ctx 调用截止时间。
     * @return 映射；未知点位返回 NotFound。
     */
    virtual core::Result<SeriesMapping> find_series(const std::string& point_id,
                                                    const core::CallContext& ctx) = 0;

    /**
     * @brief 原子登记或核对全局事件身份；同 ID 不同内容返回 Conflict。
     * @param[in] sample 包含事件 ID、点位、原时间和完整事件摘要的样本。
     * @param[in] ctx 调用截止时间。
     * @return 成功表示已提交登记或确认相同登记；失败不触发 PUB/SUB 重投。
     */
    virtual core::Status register_event(const Sample& sample, const core::CallContext& ctx) = 0;

    /**
     * @brief 持久化隔离记录；相同投递身份重试必须幂等。
     * @param[in] record 隔离记录及受限原始载荷。
     * @param[in] ctx 调用截止时间。
     * @return 持久化结果；ZeroMQ 投递本身不提供确认或重放。
     */
    virtual core::Status quarantine(const QuarantineRecord& record,
                                    const core::CallContext& ctx) = 0;
};

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
};

struct ConsumerOptions {
    std::string scope_kind;
    std::uint64_t max_items{128};
    std::uint64_t max_bytes{1024 * 1024};
    std::uint64_t max_payload_bytes{64 * 1024};
    std::int64_t min_source_time_us{0};
    std::int64_t max_future_skew_us{5 * 60 * 1000 * 1000LL};
};

struct BatchReport {
    std::uint64_t received{0};
    std::uint64_t written{0};
    std::uint64_t quarantined{0};
    std::uint64_t dropped{0};
};

class HistoryConsumer {
   public:
    /**
     * @brief 创建历史消费者，依赖由调用方持有并保持存活。
     * @param[in,out] feed ZeroMQ 原始遥测接收接口。
     * @param[in,out] repository MySQL 历史目录、账本和隔离仓库。
     * @param[in,out] series 时序存储后端。
     * @param[in] scope 接收端所属稳定站或单元 ID。
     * @param[in] options 作用域类型、单批及载荷限制。
     */
    HistoryConsumer(IHistoryFeed& feed, IHistoryRepository& repository, ITimeSeriesStore& series,
                    std::string scope, ConsumerOptions options = {});

    /**
     * @brief 处理一个有界批次；失败事件计入丢样，不假装可重放。
     * @param[in] ctx 调用截止时间。
     * @return 批次计数；读取错误作为失败返回。
     */
    core::Result<BatchReport> consume_batch(const core::CallContext& ctx);

   private:
    /**
     * @brief 处理单条投递，并维护批次统计。
     * @param[in] delivery 原始投递。
     * @param[in] ctx 调用截止时间。
     * @param[in,out] report 批次计数。
     * @return 成功表示已写入或隔离；失败事件无法由 PUB/SUB 自动重投。
     */
    core::Status process(const messaging::Delivery& delivery, const core::CallContext& ctx,
                         BatchReport& report);
    IHistoryFeed& feed_;
    IHistoryRepository& repository_;
    ITimeSeriesStore& series_;
    std::string scope_;
    ConsumerOptions options_;
};

/**
 * @brief 解析事件为有类型样本并计算完整规范化事件 SHA-256。
 * @param[in] event 遥测事件信封。
 * @param[in] options 校验时间与载荷限制。
 * @return 成功时返回样本；确定性非法数据返回 SchemaMismatch 或 InvalidArgument。
 */
core::Result<Sample> parse_sample(const messaging::EventEnvelope& event,
                                  const ConsumerOptions& options);

struct QueryCursor {
    std::int64_t source_time_us{0};
    std::string point_id;
    std::string event_id;
    std::string fingerprint;
    std::int64_t expires_at_us{0};
};

struct HistoryQuery {
    std::string scope_kind;
    std::string scope_id;
    std::vector<std::string> point_ids;
    std::int64_t start_us{0};
    std::int64_t end_us{0};
    std::uint32_t max_rows{0};
    std::optional<QueryCursor> cursor;
};

struct HistoryRow {
    Sample sample;
    PointSnapshot point;
};

struct HistoryPage {
    std::vector<HistoryRow> rows;
    std::optional<QueryCursor> next;
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

class HistoryQueryService {
   public:
    /**
     * @brief 创建应用层双库历史查询服务，依赖由调用方持有。
     * @param[in,out] repository MySQL 版本目录。
     * @param[in,out] reader 时序后端的原始样本读取器。
     */
    HistoryQueryService(IHistoryRepository& repository, ITimeSeriesReader& reader);

    /**
     * @brief 读取授权作用域内的有界一页历史，并按样本版本关联元数据。
     * @param[in] query 范围、点位与分页条件。
     * @param[in] authorized_scopes 已认证调用方可访问的作用域键，格式 kind:id。
     * @param[in] ctx 调用截止时间。
     * @return 带版本化元数据的页面；目录缺失或越权返回错误。
     */
    core::Result<HistoryPage> query(const HistoryQuery& query,
                                    const std::vector<std::string>& authorized_scopes,
                                    const core::CallContext& ctx);

   private:
    IHistoryRepository& repository_;
    ITimeSeriesReader& reader_;
};

}  // namespace ems::storage
