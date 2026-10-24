#pragma once

#include <common/database/configuration/i_device_management_store.h>

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

#include "device_management_write_rows.h"
#include "orm_device_config_repository.h"

namespace ems::database::detail {

template <class Driver>
class OrmDeviceManagementStore final : public IDeviceManagementStore {
   public:
    using Acquire = typename OrmDeviceConfigRepository<Driver>::Acquire;
    using Invalidate = typename OrmDeviceConfigRepository<Driver>::Invalidate;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /**
     * @brief 注入同一公共事务连接及错误处理。
     * @param[in] acquire 借用器，不新增连接。
     * @param[in] invalidate 失败事务失效处理。
     * @param[in] failure 驱动错误转换器。
     * @param[in] mysql 是否启用 MySQL 行锁，SQLite 测试使用 false。
     */
    OrmDeviceManagementStore(Acquire acquire, Invalidate invalidate, Failure failure,
                             bool mysql = true)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)),
          mysql_(mysql) {}

    /**
     * @brief 分页查询受支持表；列名来自反射白名单，值使用绑定参数。
     * @param[in,out] tx 公共事务。
     * @param[in] q 分页与过滤。
     * @param[in] ctx 期限。
     * @return 行与计数或错误。
     */
    core::Result<DevicePage> query(ITransaction& tx, const DeviceQuery& q,
                                   const core::CallContext& ctx) override {
        return run<DevicePage>(tx, ctx, [&](auto& db) {
            switch (q.table) {
                case DeviceTable::Device:
                    return read<CfgDeviceRow>(db, q);
                case DeviceTable::Type:
                    return read<CfgDeviceTypeRow>(db, q);
                case DeviceTable::TypePoint:
                    return read<CfgTypePointRow>(db, q);
                case DeviceTable::Template:
                    return read<CfgTemplateRow>(db, q);
                case DeviceTable::Channel:
                    return read<CfgChannelRow>(db, q);
                case DeviceTable::Binding:
                    return read<CfgBindingRow>(db, q);
                case DeviceTable::Yc:
                    return read<CfgPointYcRow>(db, q);
                case DeviceTable::Yx:
                    return read<CfgPointYxRow>(db, q);
                case DeviceTable::Yk:
                    return read<CfgPointYkRow>(db, q);
                case DeviceTable::Yt:
                    return read<CfgPointYtRow>(db, q);
                case DeviceTable::ImportJob:
                    return read<CfgImportJobRow>(db, q);
                case DeviceTable::ImportRow:
                    return read<CfgImportRow>(db, q);
                case DeviceTable::CommStat:
                    return read<OpsCommStat5mRow>(db, q);
                case DeviceTable::StateLog:
                    return read<OpsStateLogRow>(db, q);
            }
            throw invalid("unknown device table");
        });
    }

    /**
     * @brief 在调用方事务暂存受支持的可变配置或导入记录。
     * @param[in,out] tx 公共事务。
     * @param[in] record 行。
     * @param[in] ctx 期限。
     * @return 暂存状态或错误。
     */
    core::Status put(ITransaction& tx, const DeviceManagementRecord& record,
                     const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            std::visit(
                [&](const auto& source) {
                    using Row = std::decay_t<decltype(source)>;
                    auto row = source;
                    if constexpr (std::is_same_v<Row, CfgImportRow>) {
                        auto old = db.template query_s<Row>("import_job_id=? AND line_no=?",
                                                            row.import_job_id, row.line_no);
                        if (!old.empty()) row.id = old.front().id;
                        db.write(sql_row(row),
                                 old.empty() ? RecordWriteMode::Insert : RecordWriteMode::Update);
                    } else if constexpr (std::is_same_v<Row, CfgImportJobRow>) {
                        auto old = db.template query_s<Row>("import_job_id=?", row.import_job_id);
                        db.write(sql_row(row),
                                 old.empty() ? RecordWriteMode::Insert : RecordWriteMode::Update);
                    } else if constexpr (std::is_same_v<Row, CfgDeviceRow> ||
                                         std::is_same_v<Row, CfgChannelRow> ||
                                         std::is_same_v<Row, CfgBindingRow> ||
                                         std::is_same_v<Row, CfgPointYcRow> ||
                                         std::is_same_v<Row, CfgPointYxRow> ||
                                         std::is_same_v<Row, CfgPointYkRow> ||
                                         std::is_same_v<Row, CfgPointYtRow>) {
                        std::string field, key;
                        if constexpr (std::is_same_v<Row, CfgDeviceRow>) {
                            field = "device_id";
                            key = row.device_id;
                        } else if constexpr (std::is_same_v<Row, CfgChannelRow>) {
                            field = "channel_id";
                            key = row.channel_id;
                        } else if constexpr (std::is_same_v<Row, CfgBindingRow>) {
                            field = "binding_id";
                            key = row.binding_id;
                        } else {
                            field = "point_id";
                            key = row.point_id;
                        }
                        auto old = db.template query_s<Row>(field + "=?", key);
                        if (!old.empty()) {
                            row.id = old.front().id;
                            row.created_at = old.front().created_at;
                        }
                        db.write(sql_row(row),
                                 old.empty() ? RecordWriteMode::Insert : RecordWriteMode::Update);
                    } else
                        throw core::make_error(core::ErrorCode::Unsupported,
                                               core::WriteEffect::NotApplied,
                                               "read-only device table");
                },
                record);
            return core::Unit{};
        });
    }

   private:
    /** @brief 检查列来自行反射，禁止 SQL 标识符注入。
     * @param[in] field 候选列名。
     * @return 是否允许。
     */
    template <class Row>
    static bool column(const std::string& field) {
        constexpr auto names = ylt::reflection::get_member_names<Row>();
        return std::find(names.begin(), names.end(), field) != names.end();
    }

    /** @brief 构造脱敏参数错误。
     * @param[in] message 诊断说明。
     * @return 公共错误。
     */
    static core::Error invalid(const std::string& message) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                message);
    }

    /** @brief 执行固定八参数查询。
     * @param[in,out] db 当前受控连接。
     * @param[in] sql 固定或白名单构造模板。
     * @param[in] p 绑定参数。
     * @return 查询结果。
     */
    template <class Row, class Db>
    static auto bound(Db& db, const std::string& sql, const std::array<std::string, 8>& p) {
        return db.template query_s<Row>(sql, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    }

    /** @brief 执行单表有界分页及同事务计数。
     * @param[in,out] db 当前受控连接。
     * @param[in] q 查询。
     * @return 行与计数；非法字段抛公共错误。
     */
    template <class Row, class Db>
    DevicePage read(Db& db, const DeviceQuery& q) {
        if (q.limit == 0 || q.limit > 100000 || q.offset > 100000000 || q.filters.size() > 8)
            throw invalid("query bounds");
        // 列名从反射集合中选择；八个值槽全部绑定，绝不把业务值拼进 SQL。
        std::string where = "1=1";
        std::array<std::string, 8> params{};
        std::size_t i = 0;
        for (const auto& f : q.filters) {
            if (!column<Row>(f.field) || f.value.size() > 4096) throw invalid("query field");
            where += " AND `" + f.field + "`";
            where += f.op == DeviceFilterOp::Equal ? "=?" : " LIKE ? ESCAPE '!'";
            auto value = f.value;
            if (f.op == DeviceFilterOp::Prefix) {
                value.clear();
                for (char c : f.value) {
                    if (c == '!' || c == '%' || c == '_') value += '!';
                    value += c;
                }
                value += '%';
            }
            params[i++] = std::move(value);
        }
        while (i++ < 8) where += " AND ?=''";
        std::string order = q.order_by;
        if (order.empty()) order = std::string(ylt::reflection::get_member_names<Row>()[0]);
        if (!column<Row>(order)) throw invalid("sort field");

        // 锁查询不额外执行计数，避免在取站根锁之前建立不同时间的读取结果。
        DevicePage result;
        if (!q.for_update && !q.for_share) {
            auto counts = bound<std::tuple<std::int64_t>>(
                db,
                "SELECT COUNT(*) FROM " +
                    std::string(get_alias_struct_name(static_cast<Row*>(nullptr))) + " WHERE " +
                    where,
                params);
            if (counts.empty()) throw invalid("missing query count");
            result.total = std::get<0>(counts.front());
        }
        std::string tie = ",1 ASC";
        if (q.table == DeviceTable::Type || q.table == DeviceTable::TypePoint)
            tie += ",type_version ASC";
        if (q.table == DeviceTable::TypePoint) tie += ",semantic_code ASC";
        if (q.table == DeviceTable::Template) tie += ",template_version ASC";
        if (q.table == DeviceTable::CommStat) tie += ",window_start ASC";
        auto rows = bound<Row>(
            db,
            where + " ORDER BY `" + order + "`" + (q.descending ? " DESC" : " ASC") + tie +
                " LIMIT " + std::to_string(q.limit) + " OFFSET " + std::to_string(q.offset) +
                ((q.for_update && mysql_) ? " FOR UPDATE"
                                          : ((q.for_share && mysql_) ? " LOCK IN SHARE MODE" : "")),
            params);
        if (q.for_update || q.for_share) result.total = static_cast<std::int64_t>(rows.size());
        for (auto& row : rows) result.rows.emplace_back(std::move(row));
        return result;
    }

    /** @brief 执行操作并在失败后使事务失效。
     * @param[in,out] tx 公共事务。
     * @param[in] ctx 期限。
     * @param[in] operation 仓储操作。
     * @return 业务值或错误。
     */
    template <class T, class Operation>
    core::Result<T> run(ITransaction& tx, const core::CallContext& ctx, Operation operation) {
        auto acquired = acquire_(tx, ctx);
        if (!core::is_ok(acquired)) return core::error_of(acquired);
        CheckedConfigurationSession<Driver> db(*core::value_of(acquired), ctx, failure_);
        try {
            return operation(db);
        } catch (const core::Error& error) {
            invalidate_(tx);
            return error;
        } catch (...) {
            invalidate_(tx);
            return core::make_error(core::ErrorCode::Internal, core::WriteEffect::NotApplied,
                                    "device store failed");
        }
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
    bool mysql_;
};
}  // namespace ems::database::detail
