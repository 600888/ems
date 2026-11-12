/** @file records.hpp
 * @brief 日/月冻结和常用事件的记录查询模板及行列校验。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::standard {
/// 记录模板的固定定位列；不等于设备关联对象属性表或完整事件记录结构。
struct RecordDefinition {
    std::uint16_t oi;
    const char* name;
    model::Oad sequence;
    model::Oad time;
    std::vector<model::Oad> base_columns;  ///< 冻结为序号/时间，事件增加结束时间及 NULL 来源。
};

/** @brief 查询本批日/月冻结、掉电及初始化事件模板。
 * @param[in] oi 记录对象标识。
 * @return 进程生命周期内有效的不可变指针；未收录返回 nullptr。
 */
DLT698_API const RecordDefinition* find_record(std::uint16_t oi);
/** @brief 校验用于记录定位的完整日期时间，无时区和通配字段。
 * @param[in] time 年 1～9999，月份/日期按公历校验，秒为 0～59。
 * @return 成功或 invalid_value；不修改原字段。
 */
DLT698_API Result<void> validate_record_time(const model::DateTimeS& time);
/** @brief 校验记录请求、平面 OAD 列和编码资源上限。
 * @param[in] query 属性 2、特征零、索引零；空列表示采用后端配置的全部列。
 * @param[in] layout 普通关联列的设备配置。
 * @param[in] limits 列数量、RSD 嵌套及完整请求字节上限。
 * @return 成功或 OAD/列/资源错误；ROAD 列为 unsupported_service。
 * @note RSD 的业务选择语义由 provider 解释，不宣称支持所有选择器业务。
 */
DLT698_API Result<void> validate_record_query(const protocol::apdu::GetRecord& query,
                                              const DeviceLayout& layout = {},
                                              const Limits& limits = {});
/** @brief 按模板、行条件及列选择构造拥有数据的 GET Record 查询。
 * @param[in] oi 已收录记录对象 OI。
 * @param[in] rows 原始行条件；通过值移动保存。
 * @param[in] columns 非空时保留顺序；为空表示全选后端配置列。
 * @param[in] layout 关联列配置。
 * @param[in] limits 编码资源上限。
 * @return 查询或模板/选择条件/资源错误；不发起网络请求。
 */
DLT698_API Result<protocol::apdu::GetRecord> make_record_query(std::uint16_t oi, model::Rsd rows,
                                                               std::vector<model::Oad> columns = {},
                                                               const DeviceLayout& layout = {},
                                                               const Limits& limits = {});
/** @brief 按冻结时间或事件发生时间选择恰好该时刻的记录。
 * @param[in] oi 已收录记录对象 OI。
 * @param[in] time 完整公历时间，无通配字段。
 * @param[in] columns 平面 OAD 列，为空采用后端配置。
 * @param[in] layout 关联列配置。
 * @param[in] limits 编码资源上限。
 * @return 使用 Selector1 的查询或校验错误。
 */
DLT698_API Result<protocol::apdu::GetRecord> record_at(std::uint16_t oi, model::DateTimeS time,
                                                       std::vector<model::Oad> columns = {},
                                                       const DeviceLayout& layout = {},
                                                       const Limits& limits = {});
/** @brief 按前闭后开的时间区间选取记录，不设置采样间隔。
 * @param[in] oi 已收录记录对象 OI。
 * @param[in] begin 含起点的完整时间。
 * @param[in] end 不含终点的完整时间，必须大于 begin。
 * @param[in] columns 平面 OAD 列，为空采用后端配置。
 * @param[in] layout 关联列配置。
 * @param[in] limits 编码资源上限。
 * @return Selector2（interval=NULL）查询或校验错误。
 */
DLT698_API Result<protocol::apdu::GetRecord> record_between(std::uint16_t oi,
                                                            model::DateTimeS begin,
                                                            model::DateTimeS end,
                                                            std::vector<model::Oad> columns = {},
                                                            const DeviceLayout& layout = {},
                                                            const Limits& limits = {});
/** @brief 按前闭后开的 UInt32 记录序号区间构造查询。
 * @param[in] oi 已收录记录对象 OI。
 * @param[in] begin 含起点的序号。
 * @param[in] end 不含终点的序号，须大于 begin。
 * @param[in] columns 平面 OAD 列，为空采用后端配置。
 * @param[in] layout 关联列配置。
 * @param[in] limits 编码资源上限。
 * @return Selector2（interval=NULL）查询或校验错误。
 */
DLT698_API Result<protocol::apdu::GetRecord> record_sequences(std::uint16_t oi, std::uint32_t begin,
                                                              std::uint32_t end,
                                                              std::vector<model::Oad> columns = {},
                                                              const DeviceLayout& layout = {},
                                                              const Limits& limits = {});
/** @brief 校验记录单元的精确类型及资源限制。
 * @param[in] oi 记录上下文；本批事件的发生源 2024 固定为 NULL，不能据此定义其他事件的源类型。
 * @param[in] column 平面列 OAD，原始字段不改写。
 * @param[in] value 单元原始 Data。
 * @param[in] layout 普通关联列的配置。
 * @param[in] limits 编码上限。
 * @return 成功或未定义/类型/长度/资源错误。
 */
DLT698_API Result<void> validate_record_cell(std::uint16_t oi, const model::Oad& column,
                                             const model::Data& value,
                                             const DeviceLayout& layout = {},
                                             const Limits& limits = {});
/** @brief 校验记录响应的属性、列顺序、行宽、每个单元及整包编码上限。
 * @param[in] query 原始查询。
 * @param[in] result 拥有数据的响应快照；DAR 保留原码。
 * @param[in] layout 普通关联列配置。
 * @param[in] limits 响应行数/列数及编码上限。
 * @return 成功或响应错位、类型/长度/资源错误。
 */
DLT698_API Result<void> validate_record_result(const protocol::apdu::GetRecord& query,
                                               const protocol::apdu::RecordResult& result,
                                               const DeviceLayout& layout = {},
                                               const Limits& limits = {});
}  // namespace dlt698::standard
