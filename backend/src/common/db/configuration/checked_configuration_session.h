#pragma once

#include <common/database/i_database.h>

#include <functional>
#include <string>
#include <utility>

namespace ems::database::detail {
enum class RecordWriteMode { Insert, Update };

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

}  // namespace ems::database::detail
