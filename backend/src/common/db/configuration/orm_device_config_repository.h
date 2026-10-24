/**
 * @file orm_device_config_repository.h
 * @brief 设备管理配置仓储的 ORM 实现（私有实现，仅公共 MySQL 层使用）。
 *
 * 不做配置版本：写操作是单表 upsert/delete，跨表校验由配置服务负责。
 * 读取按表全量加载并设上限，防止误连到大库时拖垮进程。
 */
#pragma once

#include <common/database/configuration/i_device_config_repository.h>

#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "device_management_rows.h"
#include "orm_repository.h"

namespace ems::database::detail {

/// 设备管理配置的 ORM 实现；MySQL 与测试驱动共用同一份读写逻辑。
template <class Driver>
class OrmDeviceConfigRepository final : public IDeviceConfigRepository {
   public:
    using Acquire = std::function<core::Result<Driver*>(ITransaction&, const core::CallContext&)>;
    using Invalidate = std::function<void(ITransaction&)>;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /**
     * @brief 注入公共事务连接访问与错误处理。
     * @param[in] acquire 同上下文事务的连接借用器。
     * @param[in] invalidate 失败事务失效回调。
     * @param[in] failure 驱动错误脱敏转换器。
     */
    OrmDeviceConfigRepository(Acquire acquire, Invalidate invalidate, Failure failure)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)) {}

    /**
     * @brief 读取全部设备配置与类型目录。
     * @param[in,out] tx 公共事务。
     * @param[in] ctx 调用期限。
     * @return 全量记录或错误。
     */
    core::Result<DeviceConfigRecords> load_configuration(
        ITransaction& tx, const core::CallContext& ctx) override {
        return run<DeviceConfigRecords>(tx, ctx, [](auto& db) {
            DeviceConfigRecords records;
            records.devices = bounded<CfgDeviceRow>(db, kRowLimit);
            records.device_types = bounded<CfgDeviceTypeRow>(db, kRowLimit);
            records.type_points = bounded<CfgTypePointRow>(db, kPointLimit);
            records.channels = bounded<CfgChannelRow>(db, kRowLimit);
            records.bindings = bounded<CfgBindingRow>(db, kRowLimit);
            records.points_yc = bounded<CfgPointYcRow>(db, kPointLimit);
            records.points_yx = bounded<CfgPointYxRow>(db, kPointLimit);
            records.points_yk = bounded<CfgPointYkRow>(db, kPointLimit);
            records.points_yt = bounded<CfgPointYtRow>(db, kPointLimit);
            return records;
        });
    }

    /**
     * @brief 按稳定 ID 新增或更新设备节点。
     * @param[in,out] tx 公共事务。
     * @param[in] row 设备行。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status save_device(ITransaction& tx, const CfgDeviceRow& row,
                             const core::CallContext& ctx) override {
        return save_row<CfgDeviceRow>(tx, ctx, row, "device_id", row.device_id);
    }

    /**
     * @brief 删除设备节点。
     * @param[in,out] tx 公共事务。
     * @param[in] device_id 稳定设备 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status remove_device(ITransaction& tx, const std::string& device_id,
                               const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            require_key(device_id, "device_id is required");
            if (exists<CfgDeviceRow>(db, "parent_device_id=?", device_id))
                throw conflict("device still has child nodes");
            if (exists<CfgBindingRow>(db, "device_id=?", device_id))
                throw conflict("device is referenced by bindings");
            if (exists<CfgPointYcRow>(db, "owner_device_id=?", device_id) ||
                exists<CfgPointYxRow>(db, "owner_device_id=?", device_id) ||
                exists<CfgPointYkRow>(db, "owner_device_id=?", device_id) ||
                exists<CfgPointYtRow>(db, "owner_device_id=?", device_id))
                throw conflict("device is referenced by points");
            remove_one<CfgDeviceRow>(db, "device_id=?", device_id);
            return core::Unit{};
        });
    }

    /**
     * @brief 按稳定 ID 新增或更新通道。
     * @param[in,out] tx 公共事务。
     * @param[in] row 通道行。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status save_channel(ITransaction& tx, const CfgChannelRow& row,
                              const core::CallContext& ctx) override {
        return save_row<CfgChannelRow>(tx, ctx, row, "channel_id", row.channel_id);
    }

    /**
     * @brief 删除通道。
     * @param[in,out] tx 公共事务。
     * @param[in] channel_id 稳定通道 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status remove_channel(ITransaction& tx, const std::string& channel_id,
                                const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            require_key(channel_id, "channel_id is required");
            if (exists<CfgBindingRow>(db, "channel_id=?", channel_id))
                throw conflict("channel is referenced by bindings");
            remove_one<CfgChannelRow>(db, "channel_id=?", channel_id);
            return core::Unit{};
        });
    }

    /**
     * @brief 按稳定 ID 新增或更新绑定。
     * @param[in,out] tx 公共事务。
     * @param[in] row 绑定行。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status save_binding(ITransaction& tx, const CfgBindingRow& row,
                              const core::CallContext& ctx) override {
        return save_row<CfgBindingRow>(tx, ctx, row, "binding_id", row.binding_id);
    }

    /**
     * @brief 删除绑定。
     * @param[in,out] tx 公共事务。
     * @param[in] binding_id 稳定绑定 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status remove_binding(ITransaction& tx, const std::string& binding_id,
                                const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            require_key(binding_id, "binding_id is required");
            if (exists<CfgPointYcRow>(db, "binding_id=?", binding_id) ||
                exists<CfgPointYxRow>(db, "binding_id=?", binding_id) ||
                exists<CfgPointYkRow>(db, "binding_id=?", binding_id) ||
                exists<CfgPointYtRow>(db, "binding_id=?", binding_id))
                throw conflict("binding is referenced by points");
            remove_one<CfgBindingRow>(db, "binding_id=?", binding_id);
            return core::Unit{};
        });
    }

    /**
     * @brief 按稳定 ID 新增或更新四遥点。
     * @param[in,out] tx 公共事务。
     * @param[in] row 任意四遥点行。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status save_point(ITransaction& tx, const AnyPointRow& row,
                            const core::CallContext& ctx) override {
        return std::visit(
            [&](const auto& point) -> core::Status {
                using Row = std::decay_t<decltype(point)>;
                return save_row<Row>(tx, ctx, point, "point_id", point.point_id);
            },
            row);
    }

    /**
     * @brief 从指定点表删除点。
     * @param[in,out] tx 公共事务。
     * @param[in] kind 目标点表。
     * @param[in] point_id 稳定点 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存结果。
     */
    core::Status remove_point(ITransaction& tx, PointTableKind kind, const std::string& point_id,
                              const core::CallContext& ctx) override {
        switch (kind) {
            case PointTableKind::Yc:
                return remove_point_row<CfgPointYcRow>(tx, ctx, point_id);
            case PointTableKind::Yx:
                return remove_point_row<CfgPointYxRow>(tx, ctx, point_id);
            case PointTableKind::Yk:
                return remove_point_row<CfgPointYkRow>(tx, ctx, point_id);
            case PointTableKind::Yt:
                return remove_point_row<CfgPointYtRow>(tx, ctx, point_id);
        }
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                "unknown point table");
    }

    /**
     * @brief 递增站根修订号。
     * @param[in,out] tx 公共事务。
     * @param[in] station_device_id 站根设备 ID。
     * @param[in] ctx 调用期限。
     * @return 递增后的修订号。
     */
    core::Result<std::int64_t> bump_config_revision(
        ITransaction& tx, const std::string& station_device_id,
        const core::CallContext& ctx) override {
        return run<std::int64_t>(tx, ctx, [&](auto& db) {
            require_key(station_device_id, "station_device_id is required");
            auto rows = db.template query_s<CfgDeviceRow>("device_id=? FOR UPDATE",
                                                          station_device_id);
            if (rows.empty()) throw not_found("station device not found");
            auto row = rows.front();
            if (row.node_kind != "station") throw invalid("device is not a station root");
            row.config_revision += 1;
            row.updated_at = now_text();
            db.write(row, RecordWriteMode::Update);
            return row.config_revision;
        });
    }

   private:
    static constexpr std::size_t kRowLimit = 10000;
    static constexpr std::size_t kPointLimit = 100000;

    /**
     * @brief 在同一公共事务中执行操作并转换失败。
     * @param[in,out] tx 公共事务。
     * @param[in] ctx 调用期限。
     * @param[in] operation 受控仓储操作。
     * @return 操作结果或公共错误；任何失败均禁止继续提交该事务。
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
                                    "device configuration repository operation failed");
        }
    }

    /** @brief 单表全量读取并检查上限。 */
    template <class Row, class Db>
    static std::vector<Row> bounded(Db& db, std::size_t limit) {
        auto rows = db.template query_s<Row>("1=1 LIMIT " + std::to_string(limit + 1));
        if (rows.size() > limit)
            throw core::make_error(core::ErrorCode::ResourceExhausted,
                                   core::WriteEffect::NotApplicable,
                                   "device configuration table exceeds row limit");
        return rows;
    }

    /** @brief 判断是否存在匹配行。 */
    template <class Row, class Db, class Key>
    static bool exists(Db& db, const std::string& where, const Key& key) {
        return !db.template query_s<Row>(where + " LIMIT 1", key).empty();
    }

    /** @brief 删除一行，不存在抛 NotFound。 */
    template <class Row, class Db, class Key>
    static void remove_one(Db& db, const std::string& where, const Key& key) {
        const auto removed = db.template delete_records_s<Row>(where, key);
        if (removed == 0) throw not_found("device configuration record not found");
    }

    /**
     * @brief 按稳定业务 ID upsert 一行，保留 created_at。
     * @param[in,out] db 当前事务的受控连接。
     * @param[in] row 待写行。
     * @param[in] key_field 稳定业务 ID 字段名。
     * @param[in] key_value 稳定业务 ID 值。
     */
    template <class Row, class Db>
    static void upsert(Db& db, const Row& row, const std::string& key_field,
                       const std::string& key_value) {
        require_key(key_value, key_field + " is required");
        auto persisted = row;
        const auto timestamp = now_text();
        auto existing = db.template query_s<Row>(key_field + "=? LIMIT 1", key_value);
        if (existing.empty()) {
            if (persisted.created_at.empty()) persisted.created_at = timestamp;
            persisted.updated_at = timestamp;
            db.write(persisted, RecordWriteMode::Insert);
            return;
        }
        persisted.id = existing.front().id;
        persisted.created_at = existing.front().created_at;
        persisted.updated_at = timestamp;
        db.write(persisted, RecordWriteMode::Update);
    }

    /** @brief 供 save_* 复用的模板包装。 */
    template <class Row>
    core::Status save_row(ITransaction& tx, const core::CallContext& ctx, const Row& row,
                          const std::string& key_field, const std::string& key_value) {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            upsert<Row>(db, row, key_field, key_value);
            return core::Unit{};
        });
    }

    /** @brief 删除单张点表的行。 */
    template <class Row>
    core::Status remove_point_row(ITransaction& tx, const core::CallContext& ctx,
                                  const std::string& point_id) {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            require_key(point_id, "point_id is required");
            remove_one<Row>(db, "point_id=?", point_id);
            return core::Unit{};
        });
    }

    /** @brief 当前 UTC 时间（配置表 DATETIME(6) 只存到秒）。 */
    static std::string now_text() {
        const auto now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
        return stream.str();
    }

    /** @brief 校验稳定业务 ID 非空。 */
    static void require_key(const std::string& value, const std::string& reason) {
        if (value.empty()) throw invalid(reason);
    }

    /** @brief 参数错误。 */
    static core::Error invalid(const std::string& reason) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                reason);
    }

    /** @brief 唯一性或引用冲突。 */
    static core::Error conflict(const std::string& reason) {
        return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied, reason);
    }

    /** @brief 记录不存在。 */
    static core::Error not_found(const std::string& reason) {
        return core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplied, reason);
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
};

}  // namespace ems::database::detail
