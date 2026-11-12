/** @file time_tag.hpp
 * @brief 与宿主时区无关的时间标签日历有效性判断。
 */
#pragma once
#include <dlt698/protocol/apdu/get.hpp>

namespace dlt698::protocol::apdu {
/** @brief 检查具体日历时间与发送时标的时差是否在允许范围内。
 * @param[in] tag 原始发送标签；零间隔不限制时差，但日历须具体且合法。
 * @param[in] now 同一时间基准下的接收时间。
 * @return 有效为 true，超期为 false，非法日历/单位或年月溢出返回 invalid_value。
 * @note 秒、分、时、日使用固定时长；月、年按发送日历增加并截到目标月末，拒绝 FF 通配字段。
 */
DLT698_API Result<bool> valid_time_tag(const TimeTag& tag, const model::DateTimeS& now);
}  // namespace dlt698::protocol::apdu
