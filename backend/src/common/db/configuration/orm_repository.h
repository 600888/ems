#pragma once

#include <functional>
#include <utility>

#include "orm_rows.h"
#include "point_table_store.hpp"

namespace ems::database::detail {

/** @brief 为每个 ORM 操作检查期限并转换驱动错误；仅供公共实现使用。 */
template <class Driver>
class CheckedConfigurationSession {
   public:
    using Failure = std::function<core::Error(Driver&)>;

    /**
     * @brief 绑定当前独占驱动与期限。
     * @param[in,out] driver 独占连接。
     * @param[in] ctx 调用期限。
     * @param[in] failure 脱敏错误转换器。
     */
    CheckedConfigurationSession(Driver& driver, const core::CallContext& ctx, Failure failure)
        : driver_(driver), ctx_(ctx), failure_(std::move(failure)) {}

    /**
     * @brief 执行固定条件的实体查询。
     * @param[in] where 公共实现固定的筛选模板。
     * @param[in] args 绑定参数。
     * @return 行集合；失败抛公共 Error，由仓储捕获并使事务失效。
     */
    template <class Row, class... Args>
    auto query_s(const std::string& where = "", Args&&... args) {
        prepare();
        auto rows = driver_.orm.template query_s<Row>(where, std::forward<Args>(args)...);
        check();
        return rows;
    }

    /**
     * @brief 插入实体并读取生成 ID。
     * @param[in] row 待写记录。
     * @return 生成 ID，失败抛公共 Error。
     */
    template <class Row>
    auto get_insert_id_after_insert(const Row& row) {
        prepare();
        auto id = driver_.orm.get_insert_id_after_insert(row);
        check();
        return id;
    }

    /**
     * @brief 按绑定条件删除实体。
     * @param[in] where 固定条件模板。
     * @param[in] args 绑定参数。
     * @return 删除行数，失败抛公共 Error。
     */
    template <class Row, class... Args>
    auto delete_records_s(const std::string& where, Args&&... args) {
        prepare();
        auto count = driver_.orm.template delete_records_s<Row>(where, std::forward<Args>(args)...);
        check();
        return count;
    }

    /**
     * @brief 写入设备或通道记录。
     * @param[in] row 待写记录。
     * @param[in] mode 新增或更新。
     */
    template <class Row>
    void write(const Row& row, RecordWriteMode mode) {
        prepare();
        if (mode == RecordWriteMode::Insert)
            driver_.orm.insert(row);
        else
            driver_.orm.update(row);
        check();
    }

    /** @brief 所有驱动错误已立即转换，此入口供暂存算法使用。
     * @return 恒为 false。
     */
    bool has_error() const { return false; }

    /** @brief 不向暂存算法暴露原始驱动错误。
     * @return 脱敏错误文本。
     */
    std::string get_last_error() const { return "configuration database operation failed"; }

   private:
    /** @brief 检查单次 I/O 期限，失败抛公共 Error。 */
    void prepare() {
        auto status = driver_.prepare(ctx_);
        if (!core::is_ok(status)) throw core::error_of(status);
    }

    /** @brief 转换 ORM 错误，失败抛公共 Error。 */
    void check() {
        if (driver_.orm.has_error()) throw failure_(driver_);
    }

    Driver& driver_;
    core::CallContext ctx_;
    Failure failure_;
};

/** @brief 公共工程配置的 ORM 实现，MySQL 与测试驱动复用查询和点表暂存逻辑。 */
template <class Driver>
class OrmConfigurationRepository final : public IConfigurationRepository {
   public:
    using Acquire = std::function<core::Result<Driver*>(ITransaction&, const core::CallContext&)>;
    using Invalidate = std::function<void(ITransaction&)>;
    using Failure = typename CheckedConfigurationSession<Driver>::Failure;

    /**
     * @brief 注入公共事务连接访问与错误处理。
     * @param[in] acquire 同上下文事务的连接借用器。
     * @param[in] invalidate 失败事务失效回调。
     * @param[in] failure 驱动错误脱敏转换器。
     * @param[in] mysql MySQL 模式使用行锁，测试 SQLite 模式不发送 MySQL 方言。
     */
    OrmConfigurationRepository(Acquire acquire, Invalidate invalidate, Failure failure, bool mysql)
        : acquire_(std::move(acquire)),
          invalidate_(std::move(invalidate)),
          failure_(std::move(failure)),
          mysql_(mysql) {}

    /**
     * @brief 读取有界工程配置。
     * @param[in,out] tx 公共事务。
     * @param[in] ctx 调用期限。
     * @return 配置或错误。
     */
    core::Result<ConfigurationRecords> read_configuration(ITransaction& tx,
                                                          const core::CallContext& ctx) override {
        return run<ConfigurationRecords>(tx, ctx, [&](auto& db) {
            ConfigurationRecords result;
            result.devices = bounded<DeviceRecord>(db);
            result.channels = bounded<ChannelRecord>(db);
            result.points.yc = bounded<AnalogPoint>(db);
            result.points.yx = bounded<StatusPoint>(db);
            result.points.yk = bounded<ControlPoint>(db);
            result.points.yt = bounded<SetpointPoint>(db);
            return result;
        });
    }

    /**
     * @brief 按标识读取配置。
     * @param[in,out] tx 公共事务。
     * @param[in] id 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录，不存在返回 NotFound。
     */
    core::Result<DeviceRecord> find_device(ITransaction& tx, std::int64_t id,
                                           const core::CallContext& ctx) override {
        return run<DeviceRecord>(tx, ctx, [&](auto& db) {
            if (id <= 0) throw invalid("invalid configuration identity");
            return one<DeviceRecord>(db, "id=?", id);
        });
    }

    /**
     * @brief 按标识读取配置。
     * @param[in,out] tx 公共事务。
     * @param[in] code 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录，不存在返回 NotFound。
     */
    core::Result<DeviceRecord> find_device_by_code(ITransaction& tx, const std::string& code,
                                                   const core::CallContext& ctx) override {
        return run<DeviceRecord>(tx, ctx, [&](auto& db) {
            if (code.empty()) throw invalid("invalid configuration identity");
            return one<DeviceRecord>(db, "code=?", code);
        });
    }

    /**
     * @brief 暂存设备或通道配置。
     * @param[in,out] tx 公共事务。
     * @param[in] record 配置记录。
     * @param[in] mode 新增或更新。
     * @param[in] ctx 调用期限。
     * @return 暂存状态，失败事务必须回滚。
     */
    core::Status write_device(ITransaction& tx, const DeviceRecord& record, RecordWriteMode mode,
                              const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            lock_configuration(db);
            if (record.id <= 0 || record.code.empty() || record.code.size() > 64 ||
                (mode != RecordWriteMode::Insert && mode != RecordWriteMode::Update))
                throw invalid("invalid configuration record");
            auto persisted = record;
            if (mode == RecordWriteMode::Update)
                persisted.created_at = one<DeviceRecord>(db, "id=?", record.id).created_at;
            const auto timestamp = configuration_timestamp();
            if (persisted.created_at.empty()) persisted.created_at = timestamp;
            persisted.updated_at = timestamp;
            db.write(persisted, mode);
            return core::Unit{};
        });
    }

    /**
     * @brief 删除未被引用的记录。
     * @param[in,out] tx 公共事务。
     * @param[in] id 记录 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存状态。
     */
    core::Status remove_device(ITransaction& tx, std::int64_t id,
                               const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            lock_configuration(db);
            if (id <= 0) throw invalid("invalid configuration identity");
            one<DeviceRecord>(db, "id=?", id);
            if (!db.template query_s<ChannelRecord>("device_id=? LIMIT 1", id).empty())
                throw conflict("device is referenced by channels");
            if (!db.template query_s<AnalogPoint>("device_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<StatusPoint>("device_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<ControlPoint>("device_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<SetpointPoint>("device_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            db.template delete_records_s<DeviceRecord>("id=?", id);
            return core::Unit{};
        });
    }

    /**
     * @brief 按标识读取配置。
     * @param[in,out] tx 公共事务。
     * @param[in] id 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录，不存在返回 NotFound。
     */
    core::Result<ChannelRecord> find_channel(ITransaction& tx, std::int64_t id,
                                             const core::CallContext& ctx) override {
        return run<ChannelRecord>(tx, ctx, [&](auto& db) {
            if (id <= 0) throw invalid("invalid configuration identity");
            return one<ChannelRecord>(db, "id=?", id);
        });
    }

    /**
     * @brief 按标识读取配置。
     * @param[in,out] tx 公共事务。
     * @param[in] code 记录标识。
     * @param[in] ctx 调用期限。
     * @return 记录，不存在返回 NotFound。
     */
    core::Result<ChannelRecord> find_channel_by_code(ITransaction& tx, const std::string& code,
                                                     const core::CallContext& ctx) override {
        return run<ChannelRecord>(tx, ctx, [&](auto& db) {
            if (code.empty()) throw invalid("invalid configuration identity");
            return one<ChannelRecord>(db, "code=?", code);
        });
    }

    /**
     * @brief 暂存设备或通道配置。
     * @param[in,out] tx 公共事务。
     * @param[in] record 配置记录。
     * @param[in] mode 新增或更新。
     * @param[in] ctx 调用期限。
     * @return 暂存状态，失败事务必须回滚。
     */
    core::Status write_channel(ITransaction& tx, const ChannelRecord& record, RecordWriteMode mode,
                               const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            lock_configuration(db);
            if (record.id <= 0 || record.code.empty() || record.code.size() > 64 ||
                (mode != RecordWriteMode::Insert && mode != RecordWriteMode::Update))
                throw invalid("invalid configuration record");
            auto persisted = record;
            if (mode == RecordWriteMode::Update)
                persisted.created_at = one<ChannelRecord>(db, "id=?", record.id).created_at;
            const auto timestamp = configuration_timestamp();
            if (persisted.created_at.empty()) persisted.created_at = timestamp;
            persisted.updated_at = timestamp;
            one<DeviceRecord>(db, "id=?", record.device_id);
            for (const auto& point :
                 db.template query_s<AnalogPoint>("channel_id=? LIMIT 10001", record.id))
                if (point.device_id != record.device_id ||
                    point.protocol_type != record.protocol_type)
                    throw conflict("channel is referenced by incompatible point configuration");
            for (const auto& point :
                 db.template query_s<StatusPoint>("channel_id=? LIMIT 10001", record.id))
                if (point.device_id != record.device_id ||
                    point.protocol_type != record.protocol_type)
                    throw conflict("channel is referenced by incompatible point configuration");
            for (const auto& point :
                 db.template query_s<ControlPoint>("channel_id=? LIMIT 10001", record.id))
                if (point.device_id != record.device_id ||
                    point.protocol_type != record.protocol_type)
                    throw conflict("channel is referenced by incompatible point configuration");
            for (const auto& point :
                 db.template query_s<SetpointPoint>("channel_id=? LIMIT 10001", record.id))
                if (point.device_id != record.device_id ||
                    point.protocol_type != record.protocol_type)
                    throw conflict("channel is referenced by incompatible point configuration");

            db.write(persisted, mode);
            return core::Unit{};
        });
    }

    /**
     * @brief 删除未被引用的记录。
     * @param[in,out] tx 公共事务。
     * @param[in] id 记录 ID。
     * @param[in] ctx 调用期限。
     * @return 暂存状态。
     */
    core::Status remove_channel(ITransaction& tx, std::int64_t id,
                                const core::CallContext& ctx) override {
        return run<core::Unit>(tx, ctx, [&](auto& db) {
            lock_configuration(db);
            if (id <= 0) throw invalid("invalid configuration identity");
            one<ChannelRecord>(db, "id=?", id);
            if (!db.template query_s<AnalogPoint>("channel_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<StatusPoint>("channel_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<ControlPoint>("channel_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            if (!db.template query_s<SetpointPoint>("channel_id=? LIMIT 1", id).empty())
                throw conflict("configuration is referenced by points");
            db.template delete_records_s<ChannelRecord>("id=?", id);
            return core::Unit{};
        });
    }

    /**
     * @brief 暂存四遥点表，不提交事务。
     * @param[in,out] tx 公共事务。
     * @param[in] data 点表草稿。
     * @param[in] channel_id 通道 ID。
     * @param[in] device_id 设备 ID。
     * @param[in] mode 导入模式。
     * @param[in] ctx 调用期限。
     * @return 暂存记录或错误。
     */
    core::Result<PointTableData> stage_point_table(ITransaction& tx, const PointTableData& data,
                                                   std::int64_t channel_id, std::int64_t device_id,
                                                   PointTableImportMode mode,
                                                   const core::CallContext& ctx) override {
        return run<PointTableData>(tx, ctx, [&](auto& db) {
            if (channel_id <= 0 || device_id <= 0 ||
                (mode != PointTableImportMode::Append && mode != PointTableImportMode::Replace))
                throw invalid("invalid point table target or mode");
            if (data.yc.size() + data.yx.size() + data.yk.size() + data.yt.size() > 10000)
                throw invalid("point table exceeds row limit");
            auto validate = [&](const auto& points) {
                for (const auto& point : points)
                    if (point.channel_id != channel_id || point.device_id != device_id ||
                        point.code.empty() || point.id != 0)
                        throw invalid("point table contains foreign scope or persistent identity");
            };
            validate(data.yc);
            validate(data.yx);
            validate(data.yk);
            validate(data.yt);
            PointTableImportResult staged;
            staged.data = data;
            if (!store_point_table(db, staged, channel_id, device_id, mode, mysql_))
                throw conflict(staged.errors.front().reason.c_str());
            return staged.data;
        });
    }

   private:
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
                                    "configuration repository operation failed");
        }
    }

    /**
     * @brief 与点表导入采用相同锁顺序串行化配置写入。
     * @param[in,out] db 当前事务的受控连接。
     */
    template <class Db>
    void lock_configuration(Db& db) {
        if (mysql_) {
            auto rows =
                db.template query_s<ChannelRecord>("1=1 ORDER BY id LIMIT 10001 FOR UPDATE");
            if (rows.size() > 10000)
                throw core::make_error(core::ErrorCode::ResourceExhausted,
                                       core::WriteEffect::NotApplied,
                                       "configuration table exceeds row limit");
        }
    }

    /**
     * @brief 读取最多 10000 行的单表配置。
     * @param[in,out] db 已检查期限的连接。
     * @return 有界行集合，超出限制抛公共 Error。
     */
    template <class Row, class Db>
    static auto bounded(Db& db) {
        auto rows = db.template query_s<Row>("1=1 ORDER BY id LIMIT 10001");
        if (rows.size() > 10000)
            throw core::make_error(core::ErrorCode::ResourceExhausted,
                                   core::WriteEffect::NotApplicable,
                                   "configuration table exceeds row limit");
        return rows;
    }

    /**
     * @brief 按绑定条件读取一条配置。
     * @param[in,out] db 已检查期限的连接。
     * @param[in] where 固定查询条件。
     * @param[in] key 绑定标识。
     * @return 唯一记录，不存在或不唯一抛公共 Error。
     */
    template <class Row, class Db, class Key>
    static Row one(Db& db, const std::string& where, const Key& key) {
        auto rows = db.template query_s<Row>(where + " LIMIT 2", key);
        if (rows.empty())
            throw core::make_error(core::ErrorCode::NotFound, core::WriteEffect::NotApplicable,
                                   "configuration record not found");
        if (rows.size() != 1) throw conflict("configuration identity is not unique");
        return rows.front();
    }

    /**
     * @brief 创建参数错误。
     * @param[in] reason 脱敏原因。
     * @return 未应用错误。
     */
    static core::Error invalid(const char* reason) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplied,
                                reason);
    }

    /**
     * @brief 创建配置冲突错误。
     * @param[in] reason 脱敏原因。
     * @return 未应用错误。
     */
    static core::Error conflict(const char* reason) {
        return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied, reason);
    }

    Acquire acquire_;
    Invalidate invalidate_;
    Failure failure_;
    bool mysql_;
};
}  // namespace ems::database::detail
