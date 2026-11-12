/** @file memory_records.hpp
 * @brief 有界、只读的标准记录模拟后端，保存配置列的记录投影。
 */
#pragma once
#include <dlt698/service/standard_object.hpp>
#include <dlt698/standard/records.hpp>

namespace dlt698::service {
/// 后端预算；达到查询上限返回拒绝 DAR=3，不静默截断。
struct RecordLimits {
    std::size_t max_rows = 1024;
    std::size_t max_result_rows = 256;
    std::size_t max_columns = 32;
    std::size_t max_snapshot_bytes = 1024 * 1024;  ///< 整个数据集的行编码内容预算。
};

class MemoryRecords final : public IObjectProvider {
   public:
    /** @brief 创建标准记录模拟后端，不自动产生冻结或检测事件。
     * @param[in] oi 日/月冻结、掉电或初始化事件的 OI。
     * @param[in] columns 存储列及顺序，必须含模板全部基本列；为空采用基本列。
     * @param[in] layout 普通关联列的设备配置。
     * @param[in] limits 每行 Data 及响应编码资源上限。
     * @param[in] record_limits 数据集/结果行数、列数及编码内容预算，均须大于零。
     * @return 共享只读 provider 或定义/配置/资源错误；初始记录为空。
     */
    DLT698_API static Result<std::shared_ptr<MemoryRecords>> create(
        std::uint16_t oi, std::vector<model::Oad> columns = {},
        const standard::DeviceLayout& layout = {}, const Limits& limits = {},
        const RecordLimits& record_limits = {});
    /** @brief 释放后端；已返回的值和 Session 已保存的分块快照仍独立有效。 */
    DLT698_API ~MemoryRecords() override;
    /** @brief 校验后原子替换整个数据集，失败保留旧快照。
     * @param[in] rows 每行按配置列顺序；序号须严格递增，定位时间须完整有效。
     * @return 成功或类型/顺序/长度/资源错误；事件结束时间保留原始未指定字段。
     * @note 并发读取先取得不可变快照后释放锁；替换不改变已返回记录，并发替换以后完成者为准。
     */
    DLT698_API Result<void> replace_rows(std::vector<protocol::apdu::RecordRow> rows);
    /** @brief 普通 GET 不返回投影伪装成完整记录表。
     * @param[in] attribute 原始 OAD。
     * @return 本对象属性 2 为 DAR=5，其余为 DAR=4；GET Record 才返回配置列。
     */
    DLT698_API ObjectValue read(const model::Oad& attribute) override;
    /** @brief 查询当前快照，按存储序号顺序返回。
     * @param[in] query 支持 RSD 0、1（时间/序号）、2（同类区间且间隔 NULL）、9（上第 n 次）。
     * @return 配置列全选或指定平面 OAD 列；空命中为成功空行，保留表头。
     * @note ROAD、其他 RSD 或非 NULL 间隔返回 DAR=3；非法选择为 8，未存储列为 4，超过查询预算为 3。
     * @note 不实现表计集合、采样间隔、事件上报状态/专用字段和数据库业务。
     */
    DLT698_API protocol::apdu::RecordResult read_record(
        const protocol::apdu::GetRecord& query) override;

   private:
    struct Impl;
    /** @brief 保存经工厂校验的独占实现。
     * @param[in] impl 被移动接管的非空实现。
     */
    explicit MemoryRecords(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace dlt698::service
