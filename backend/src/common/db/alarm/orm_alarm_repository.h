#pragma once

#include <common/database/alarm/repositories.h>
#include <common/database/identity/repositories.h>

#include <limits>
#include <nlohmann/json.hpp>

#include "../configuration/orm_device_config_repository.h"
#include "alarm_rows.h"

namespace ems::database::detail {

/** @brief 使用调用方事务维护当前状态和时序归档队列。 */
template <class Driver>
class OrmAlarmRepository final : public IAlarmRepository {
   public:
    using Acquire = typename OrmDeviceConfigRepository<Driver>::Acquire;
    using Invalidate = typename OrmDeviceConfigRepository<Driver>::Invalidate;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /**
     * @brief 装配公共事务及审计仓储。
     * @param[in] acquire 事务连接借用器。
     * @param[in] invalidate 事务失效回调。
     * @param[in] failure 驱动错误转换器。
     * @param[in] audit 与告警仓储共享事务的审计仓储。
     * @param[in] row_lock 是否使用 FOR UPDATE，SQLite 测试使用数据库写事务。
     */
    OrmAlarmRepository(Acquire acquire, Invalidate invalidate, Failure failure,
                       std::shared_ptr<IAuditRepository> audit, bool row_lock = true)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)),
          audit_(std::move(audit)),
          lock_(row_lock ? " FOR UPDATE" : "") {}

    /**
     * @brief 原子暂存生命周期、归档事件和首次确认审计。
     * @param[in,out] tx 调用方事务。
     * @param[in] change 已授权且设备归属已核验的事件。
     * @param[in] ctx 调用期限。
     * @return 暂存结果或错误。仓储不提交事务。
     */
    core::Result<AlarmChangeResult> apply(ITransaction& tx, const AlarmChange& change,
                                          const core::CallContext& ctx) override {
        return run<AlarmChangeResult>(tx, ctx, [&](auto& db) {
            const auto event = encode_event(change);
            const auto station_id = change.station_id.value();
            auto position = get_progress(db, station_id, true);
            if (!position) {
                // 两个首次写入者争用唯一站键时，失败一方重开事务后重试。
                db.write(AlarmProgressRow{0, station_id}, RecordWriteMode::Insert);
                position = get_progress(db, station_id, true);
                if (!position) reject(core::ErrorCode::CorruptData, "alarm progress missing");
            }
            auto previous =
                db.template query_s<AlarmOutboxRow>("event_id=?", change.event_id.value());
            if (!previous.empty()) {
                if (previous.front().event_json != event)
                    reject(core::ErrorCode::Conflict, "alarm event identity conflict");
                return AlarmChangeResult::Duplicate;
            }

            const auto alarm_id = change.alarm_id.value();
            auto rows = db.template query_s<AlarmControlRow>("alarm_id=?" + lock_, alarm_id);
            const auto occurrence = std::get_if<AlarmOccurrence>(&change.change);
            const auto ack = std::get_if<AlarmAcknowledgement>(&change.change);
            if (occurrence && !rows.empty())
                reject(core::ErrorCode::Conflict, "alarm already occurred");
            if (!occurrence && (rows.empty() || rows.front().station_id != station_id ||
                                rows.front().device_id != change.device_id.value()))
                reject(core::ErrorCode::NotFound, "alarm instance missing");

            AlarmControlRow row;
            Json state;
            if (occurrence) {
                row.alarm_id = alarm_id;
                row.station_id = station_id;
                row.device_id = change.device_id.value();
                row.active_key = occurrence->active_key;
                row.occurred_at_us = change.event_time_us;
                state = Json::parse(event).at("occurrence");
                state.erase("active_key");
                state["alarm_id"] = row.alarm_id;
                state["station_id"] = row.station_id;
                state["device_id"] = row.device_id;
                state["occurred_at_us"] = row.occurred_at_us;
                state["cleared_at_us"] = nullptr;
                state["acknowledgement"] = nullptr;
            } else {
                row = std::move(rows.front());
                if (ack && row.acknowledged_at_us) return AlarmChangeResult::AlreadyAcknowledged;
                if (change.event_time_us < row.occurred_at_us)
                    reject(core::ErrorCode::Conflict, "alarm change precedes occurrence");
                state = Json::parse(row.state_json);
                if (ack) {
                    row.acknowledged_at_us = change.event_time_us;
                    state["acknowledgement"] = Json::parse(event).at("acknowledgement");
                    state["acknowledgement"]["at_us"] = change.event_time_us;
                } else {
                    if (row.cleared_at_us)
                        reject(core::ErrorCode::Conflict, "alarm already cleared");
                    row.active_key.reset();
                    row.cleared_at_us = change.event_time_us;
                    state["cleared_at_us"] = change.event_time_us;
                }
            }
            if (position->committed_seq == std::numeric_limits<std::int64_t>::max())
                reject(core::ErrorCode::ResourceExhausted, "alarm stream sequence exhausted");

            ++position->committed_seq;
            row.state_json = state.dump();
            db.write(row, occurrence ? RecordWriteMode::Insert : RecordWriteMode::Update);
            const auto kind = kind_of(change);
            db.write(AlarmOutboxRow{0, change.event_id.value(), alarm_id, station_id, row.device_id,
                                    position->committed_seq, static_cast<std::int32_t>(kind),
                                    change.event_time_us, event, row.state_json},
                     RecordWriteMode::Insert);
            db.write(*position, RecordWriteMode::Update);
            if (ack) append_audit(tx, change, *ack, ctx);
            return AlarmChangeResult::Applied;
        });
    }

    /** @brief 读取当前实例。 @param[in,out] tx 事务。 @param[in] station_id 站 ID。
     * @param[in] alarm_id 告警 ID。 @param[in] ctx 期限。 @return 可空状态或错误。 */
    core::Result<std::optional<AlarmState>> find(ITransaction& tx,
                                                 const core::StationId& station_id,
                                                 const model::AlarmId& alarm_id,
                                                 const core::CallContext& ctx) override {
        return run<std::optional<AlarmState>>(tx, ctx, [&](auto& db) -> std::optional<AlarmState> {
            auto rows = db.template query_s<AlarmControlRow>("station_id=? AND alarm_id=?",
                                                             station_id.value(), alarm_id.value());
            if (rows.empty()) return std::nullopt;
            return state_of(rows.front());
        });
    }

    /** @brief 按游标读取活动实例。 @param[in,out] tx 事务。 @param[in] station_id 站 ID。
     * @param[in] after 起始排他 ID。 @param[in] ctx 期限。 @return 至多 128 条记录。 */
    core::Result<std::vector<AlarmState>> active(ITransaction& tx,
                                                 const core::StationId& station_id,
                                                 const model::AlarmId& after,
                                                 const core::CallContext& ctx) override {
        return run<std::vector<AlarmState>>(tx, ctx, [&](auto& db) {
            auto rows = db.template query_s<AlarmControlRow>(
                "station_id=? AND cleared_at_us IS NULL AND alarm_id>? ORDER BY alarm_id LIMIT 128",
                station_id.value(), after.value());
            std::vector<AlarmState> states;
            states.reserve(rows.size());
            for (const auto& row : rows) states.push_back(state_of(row));
            return states;
        });
    }

    /** @brief 单次读取活动快照。 @param[in,out] tx 事务。 @param[in] station_id 站 ID。
     * @param[in] ctx 期限。 @return 快照或超出预算错误。 */
    core::Result<std::vector<AlarmState>> active_snapshot(ITransaction& tx,
                                                          const core::StationId& station_id,
                                                          const core::CallContext& ctx) override {
        return run<std::vector<AlarmState>>(tx, ctx, [&](auto& db) {
            const auto rows = db.template query_s<AlarmControlRow>(
                "station_id=? AND cleared_at_us IS NULL LIMIT " +
                    std::to_string(kMaxActiveAlarmRows + 1),
                station_id.value());
            if (rows.size() > kMaxActiveAlarmRows)
                reject(core::ErrorCode::ResourceExhausted, "active alarm query budget exceeded");
            std::vector<AlarmState> states;
            states.reserve(rows.size());
            for (const auto& row : rows) states.push_back(state_of(row));
            return states;
        });
    }

    /** @brief 读取已提交及已归档位置。 @param[in,out] tx 事务。 @param[in] station_id 站 ID。
     * @param[in] ctx 期限。 @return 两个位置或错误。 */
    core::Result<AlarmArchiveProgress> progress(ITransaction& tx, const core::StationId& station_id,
                                                const core::CallContext& ctx) override {
        return run<AlarmArchiveProgress>(tx, ctx, [&](auto& db) {
            auto row = get_progress(db, station_id.value(), false);
            if (!row) return AlarmArchiveProgress{};
            return AlarmArchiveProgress{row->committed_seq, row->archived_seq};
        });
    }

    /** @brief 读取固定边界内的尾部。 @param[in,out] tx 事务。 @param[in] station_id 站 ID。
     * @param[in] bounds 固定边界。 @param[in] alarm_id 可选实例 ID。
     * @param[in] ctx 期限。 @return 有界事件列表或错误。 */
    core::Result<std::vector<AlarmArchiveItem>> pending(ITransaction& tx,
                                                        const core::StationId& station_id,
                                                        const AlarmArchiveProgress& bounds,
                                                        const model::AlarmId& alarm_id,
                                                        const core::CallContext& ctx) override {
        return run<std::vector<AlarmArchiveItem>>(tx, ctx, [&](auto& db) {
            if (bounds.archived_seq < 0 || bounds.committed_seq < bounds.archived_seq)
                reject(core::ErrorCode::InvalidArgument, "invalid alarm archive bounds");
            const auto size = bounds.committed_seq - bounds.archived_seq;
            if (alarm_id.empty() && size > kMaxPendingAlarmRows)
                reject(core::ErrorCode::ResourceExhausted, "alarm archive backlog exceeds budget");
            if (size == 0) return std::vector<AlarmArchiveItem>{};
            const std::string where = "station_id=? AND stream_seq>? AND stream_seq<=?";
            auto rows =
                alarm_id.empty()
                    ? db.template query_s<AlarmOutboxRow>(
                          where + " ORDER BY stream_seq LIMIT " +
                              std::to_string(kMaxPendingAlarmRows + 1),
                          station_id.value(), bounds.archived_seq, bounds.committed_seq)
                    : db.template query_s<AlarmOutboxRow>(
                          where + " AND alarm_id=? ORDER BY stream_seq LIMIT 4", station_id.value(),
                          bounds.archived_seq, bounds.committed_seq, alarm_id.value());
            if (alarm_id.empty() ? rows.size() != static_cast<std::size_t>(size) : rows.size() > 3)
                reject(core::ErrorCode::CorruptData, "invalid alarm outbox range");
            std::vector<AlarmArchiveItem> items;
            for (auto& row : rows) items.push_back(archive_item(std::move(row)));
            return items;
        });
    }

    /** @brief 读取连续归档位置之后的第一条。 @param[in,out] tx 事务。
     * @param[in] station_id 站 ID。 @param[in] ctx 期限。 @return 可空归档项或错误。 */
    core::Result<std::optional<AlarmArchiveItem>> next_for_archive(
        ITransaction& tx, const core::StationId& station_id,
        const core::CallContext& ctx) override {
        return run<std::optional<AlarmArchiveItem>>(
            tx, ctx, [&](auto& db) -> std::optional<AlarmArchiveItem> {
                auto position = get_progress(db, station_id.value(), false);
                if (!position || position->archived_seq == position->committed_seq)
                    return std::nullopt;
                auto row = get_delivery(db, station_id.value(), position->archived_seq + 1);
                if (!row) reject(core::ErrorCode::CorruptData, "invalid alarm outbox entry");
                return archive_item(std::move(*row));
            });
    }

    /** @brief 登记已写入时序库的连续事件。 @param[in,out] tx 事务。
     * @param[in] item 已成功归档的事件。 @param[in] ctx 期限。 @return 暂存结果。 */
    core::Status mark_archived(ITransaction& tx, const AlarmArchiveItem& item,
                               const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            const auto station_id = item.station_id.value();
            auto position = get_progress(db, station_id, true);
            if (!position || item.stream_seq <= 0 || item.stream_seq > position->committed_seq ||
                item.stream_seq - 1 > position->archived_seq)
                reject(core::ErrorCode::Conflict, "alarm archive sequence mismatch");
            auto row = get_delivery(db, station_id, item.stream_seq);
            if (!row || row->event_id != item.event_id.value() ||
                row->event_json != item.event_json || row->state_json != item.state_json)
                reject(core::ErrorCode::Conflict, "alarm archive identity mismatch");
            if (item.stream_seq > position->archived_seq) {
                position->archived_seq = item.stream_seq;
                db.write(*position, RecordWriteMode::Update);
            }
            return core::Unit{};
        });
    }

   private:
    using Json = nlohmann::json;
    using Session = CheckedConfigurationSession<Driver>;

    /** @brief 拒绝操作并使外层事务失效。 @param[in] code 错误码。
     * @param[in] reason 脱敏诊断。 */
    [[noreturn]] static void reject(core::ErrorCode code, const char* reason) {
        throw core::make_error(code, core::WriteEffect::NotApplied, reason);
    }

    /** @brief 检查绑定参数长度与内嵌空字符。 @param[in] value 文本。
     * @param[in] max 字节上限。 @param[in] required 是否必须非空。 */
    static void bounded(const std::string& value, std::size_t max, bool required = false) {
        if (value.size() > max || (required && value.empty()) || value.find('\0') != value.npos)
            reject(core::ErrorCode::InvalidArgument, "invalid alarm text");
    }

    /** @brief 获取生命周期类型。 @param[in] event 事件。 @return 类型。 */
    static model::AlarmLifecycle kind_of(const AlarmChange& event) {
        if (std::holds_alternative<AlarmOccurrence>(event.change))
            return model::AlarmLifecycle::Occurred;
        if (std::holds_alternative<AlarmAcknowledgement>(event.change))
            return model::AlarmLifecycle::Acknowledged;
        return model::AlarmLifecycle::Cleared;
    }

    /** @brief 检查并编码不可变载荷。 @param[in] event 事件。 @return 规范 JSON。 */
    static std::string encode_event(const AlarmChange& event) {
        bounded(event.event_id.value(), 64, true);
        bounded(event.alarm_id.value(), 64, true);
        bounded(event.station_id.value(), 64, true);
        bounded(event.device_id.value(), 64, true);
        bounded(event.detail, 2048);
        if (event.event_time_us < 0 || event.event_time_us > kMaxAlarmArchiveTimeUs)
            reject(core::ErrorCode::InvalidArgument, "invalid alarm time");
        Json json = {{"event_id", event.event_id.value()},
                     {"alarm_id", event.alarm_id.value()},
                     {"station_id", event.station_id.value()},
                     {"device_id", event.device_id.value()},
                     {"event_time_us", event.event_time_us},
                     {"kind", model::to_string(kind_of(event))},
                     {"detail", event.detail}};
        if (const auto* data = std::get_if<AlarmOccurrence>(&event.change)) {
            bounded(data->active_key, 128, true);
            bounded(data->alarm_code, 64, true);
            bounded(data->name, 512, true);
            bounded(data->device_name, 512);
            bounded(data->device_path, 2048);
            bounded(data->trigger_summary, 2048);
            if (data->severity != model::AlarmSeverity::Critical &&
                data->severity != model::AlarmSeverity::Warning &&
                data->severity != model::AlarmSeverity::Info)
                reject(core::ErrorCode::InvalidArgument, "invalid alarm severity");
            json["occurrence"] = {{"active_key", data->active_key},
                                  {"alarm_code", data->alarm_code},
                                  {"name", data->name},
                                  {"severity", model::to_string(data->severity)},
                                  {"device_name", data->device_name},
                                  {"device_path", data->device_path},
                                  {"trigger_summary", data->trigger_summary}};
            for (const auto& [key, value] :
                 std::initializer_list<std::pair<const char*, const std::string&>>{
                     {"point_id", data->point_id},
                     {"point_name", data->point_name},
                     {"trigger_condition", data->trigger_condition},
                     {"recovery_condition", data->recovery_condition},
                     {"trigger_value", data->trigger_value}}) {
                bounded(value, std::string_view(key) == "point_id" ? 64 : 512);
                if (!value.empty()) json["occurrence"][key] = value;
            }
        }
        if (const auto* ack = std::get_if<AlarmAcknowledgement>(&event.change)) {
            bounded(ack->user_id, 64, true);
            bounded(ack->display_name, 512, true);
            bounded(ack->remark, 2000, true);
            json["acknowledgement"] = {{"user_id", ack->user_id},
                                       {"display_name", ack->display_name},
                                       {"remark", ack->remark}};
        }
        try {
            return json.dump();
        } catch (const Json::type_error&) {
            reject(core::ErrorCode::InvalidArgument, "invalid alarm UTF-8");
        }
    }

    /** @brief 映射公共状态。 @param[in] row 持久行。 @return 当前状态。 */
    static AlarmState state_of(const AlarmControlRow& row) {
        return {model::AlarmId(row.alarm_id),
                core::StationId(row.station_id),
                core::DeviceId(row.device_id),
                row.active_key,
                row.occurred_at_us,
                row.cleared_at_us,
                row.acknowledged_at_us,
                row.state_json};
    }

    /** @brief 映射原始归档载荷。 @param[in] row 待移动的队列行。 @return 公共事件。 */
    static AlarmArchiveItem archive_item(AlarmOutboxRow row) {
        if (row.kind < 0 || row.kind > 2)
            reject(core::ErrorCode::CorruptData, "invalid alarm outbox entry");
        return {core::EventId(std::move(row.event_id)),
                model::AlarmId(std::move(row.alarm_id)),
                core::StationId(std::move(row.station_id)),
                core::DeviceId(std::move(row.device_id)),
                static_cast<model::AlarmLifecycle>(row.kind),
                row.event_time_us,
                row.stream_seq,
                std::move(row.event_json),
                std::move(row.state_json)};
    }

    /** @brief 读取并验证站进度。 @param[in,out] db 受检连接。 @param[in] station_id 站 ID。
     * @param[in] lock 是否写锁。 @return 可空进度。 */
    std::optional<AlarmProgressRow> get_progress(Session& db, const std::string& station_id,
                                                 bool lock) {
        auto rows = db.template query_s<AlarmProgressRow>(
            "station_id=?" + (lock ? lock_ : std::string{}), station_id);
        if (rows.empty()) return std::nullopt;
        const auto& row = rows.front();
        if (row.archived_seq < 0 || row.committed_seq < row.archived_seq)
            reject(core::ErrorCode::CorruptData, "invalid alarm archive progress");
        return row;
    }

    /** @brief 读取不可变归档行。 @param[in,out] db 受检连接。 @param[in] station_id 站 ID。
     * @param[in] seq 事件序号。 @return 可空归档行。 */
    static std::optional<AlarmOutboxRow> get_delivery(Session& db, const std::string& station_id,
                                                      std::int64_t seq) {
        auto rows =
            db.template query_s<AlarmOutboxRow>("station_id=? AND stream_seq=?", station_id, seq);
        if (rows.empty()) return std::nullopt;
        return std::move(rows.front());
    }

    /** @brief 在同一事务追加首次确认审计。 @param[in,out] tx 事务。
     * @param[in] change 确认事件。 @param[in] ack 可信确认者。 @param[in] ctx 期限。 */
    void append_audit(ITransaction& tx, const AlarmChange& change, const AlarmAcknowledgement& ack,
                      const core::CallContext& ctx) {
        if (!audit_) reject(core::ErrorCode::NotReady, "alarm audit unavailable");
        AuditRecord row;
        row.event_id = change.event_id.value();
        row.actor_user_id = ack.user_id;
        row.resource_id = change.alarm_id.value();
        row.action = "alarm.acknowledge";
        row.resource_type = "alarm";
        row.outcome = "success";
        row.request_id = ctx.trace_id.empty() ? row.event_id : ctx.trace_id;
        row.details =
            Json{{"station_id", change.station_id.value()}, {"device_id", change.device_id.value()}}
                .dump();
        row.occurred_at = core::format_rfc3339_utc(change.event_time_us);
        if (row.occurred_at.size() != 24)
            reject(core::ErrorCode::InvalidArgument, "invalid alarm audit time");
        row.occurred_at[10] = ' ';
        row.occurred_at.pop_back();
        const auto result = audit_->append(tx, row, ctx);
        if (!core::is_ok(result)) throw core::error_of(result);
    }

    /** @brief 执行受检操作，出错时禁止提交。 @param[in,out] tx 事务。
     * @param[in] ctx 期限。 @param[in] op 仓储操作。 @return 结果或脱敏错误。 */
    template <class T, class Op>
    core::Result<T> run(ITransaction& tx, const core::CallContext& ctx, Op op) {
        auto acquired = acquire_(tx, ctx);
        if (!core::is_ok(acquired)) return core::error_of(acquired);
        Session db(*core::value_of(acquired), ctx, failure_);
        try {
            return op(db);
        } catch (const core::Error& error) {
            invalidate_(tx);
            return error;
        } catch (const Json::exception&) {
            invalidate_(tx);
            return core::make_error(core::ErrorCode::CorruptData, core::WriteEffect::NotApplied,
                                    "invalid persisted alarm state");
        } catch (...) {
            invalidate_(tx);
            return core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied,
                                    "alarm repository failed");
        }
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
    std::shared_ptr<IAuditRepository> audit_;
    std::string lock_;
};

}  // namespace ems::database::detail
