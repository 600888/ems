/**
 * @file metrics.h
 * @brief 指标接口（design.md 第 12.1 节）
 *
 * 至少提供：池等待/占用、操作延迟/错误/Unknown 数、配置 active/applied
 * 差异、实时冲突/陈旧/旧来源拒绝、事件积压/pending/reclaim、outbox
 * 最旧未完成时长、日志未确认字节/最老年龄/磁盘余量。按进程/模块/
 * 后端等低基数字段聚合；device_id、event_id 不作为默认指标标签。
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace ems::observability {

class IMetrics {
   public:
    /** @brief 销毁指标接口。 */
    virtual ~IMetrics() = default;

    /// 累加计数（如错误数、Unknown 数、拒绝数）
    /// @param[in] name 对象名称。
    /// @param[in] delta 变化量。
    /// @param[in] labels_json JSON 格式的标签数据。
    virtual void add(std::string_view name, std::uint64_t delta,
                     std::string_view labels_json = {}) = 0;

    /// 设置瞬时值（如池占用、队列长度、磁盘余量）
    /// @param[in] name 对象名称。
    /// @param[in] value 待写入的值。
    /// @param[in] labels_json JSON 格式的标签数据。
    virtual void set_gauge(std::string_view name, double value,
                           std::string_view labels_json = {}) = 0;

    /// 记录一次延迟观测（毫秒；内部聚合 P95/P99）
    /// @param[in] name 对象名称。
    /// @param[in] value_ms 毫秒数值。
    /// @param[in] labels_json JSON 格式的标签数据。
    virtual void observe_latency_ms(std::string_view name, double value_ms,
                                    std::string_view labels_json = {}) = 0;
};

}  // namespace ems::observability
