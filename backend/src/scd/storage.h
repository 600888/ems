#pragma once

#include <common/core/call_context.h>
#include <common/core/result.h>
#include <common/database/history/repositories.h>
#include <common/database/i_database.h>
#include <common/database/timeseries/store.h>
#include <common/messaging/delivery.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "telemetry_feed.h"

namespace ems::storage {

using database::HistoryQuery;
using database::ITimeSeriesDatabase;
using database::ITimeSeriesReader;
using database::ITimeSeriesStore;
using database::PointSnapshot;
using database::QuarantineRecord;
using database::QueryCursor;
using database::Sample;
using database::SampleValue;
using database::SeriesMapping;
using database::ValueType;

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
     * @param[in,out] database 公共数据库上下文，目录与身份登记使用同一事务。
     * @param[in,out] repository 公共历史目录。
     * @param[in,out] events 公共事件身份与隔离仓储，不自行提交。
     * @param[in,out] series 时序存储后端。
     * @param[in] scope 接收端所属稳定站或单元 ID。
     * @param[in] options 作用域类型、单批及载荷限制。
     */
    HistoryConsumer(IHistoryFeed& feed, database::IDatabase& database,
                    database::IHistoryCatalogRepository& repository,
                    database::IHistoryEventRepository& events, ITimeSeriesStore& series,
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
    database::IDatabase& database_;
    database::IHistoryCatalogRepository& repository_;
    database::IHistoryEventRepository& events_;
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

struct HistoryRow {
    Sample sample;
    PointSnapshot point;
};

struct HistoryPage {
    std::vector<HistoryRow> rows;
    std::optional<QueryCursor> next;
};

class HistoryQueryService {
   public:
    /**
     * @brief 创建应用层双库历史查询服务，依赖由调用方持有。
     * @param[in,out] database 公共数据库上下文，时序扫描期间不持有其事务。
     * @param[in,out] repository MySQL 版本目录。
     * @param[in,out] reader 时序后端的原始样本读取器。
     */
    HistoryQueryService(database::IDatabase& database,
                        database::IHistoryCatalogRepository& repository, ITimeSeriesReader& reader);

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
    database::IDatabase& database_;
    database::IHistoryCatalogRepository& repository_;
    ITimeSeriesReader& reader_;
};

}  // namespace ems::storage
