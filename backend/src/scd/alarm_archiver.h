#pragma once

#include <common/database/i_database.h>
#include <common/database/timeseries/store.h>

namespace ems::storage {

/**
 * @brief 顺序处理单站至多 32 条告警，写时序库时释放关系库事务。
 * @param[in,out] db 公共事务入口。
 * @param[in,out] alarms 当前状态和归档队列的公共仓储。
 * @param[in,out] series 支持幂等重投的告警时序写入端口。
 * @param[in] station_id 稳定站 ID。
 * @param[in] ctx 整批调用期限。
 * @return 本批已确认归档数量，或首个错误。错误前已提交的位置保持有效。
 */
core::Result<std::size_t> archive_alarm_batch(database::IDatabase& db,
                                              database::IAlarmRepository& alarms,
                                              database::IAlarmTimeSeriesStore& series,
                                              const core::StationId& station_id,
                                              const core::CallContext& ctx);

}  // namespace ems::storage
