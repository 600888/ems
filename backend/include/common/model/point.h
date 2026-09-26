/**
 * @file point.h
 * @brief 测点模型：标准点定义、点实例与类型化样本（design.md 第 5.3/6.1 节）
 *
 * - PointValue 使用 variant<bool,int64,uint64,double,string>，外层 optional 表示无值；
 *   禁止将所有值转 double，禁止 NaN/Inf。
 * - telemetry/status/event/command 分开；命令和反馈分点（feedback_of 指回命令点）。
 * - 配置禁用、配置未绑定、通信离线、值无效、值陈旧是不同质量概念，不能折叠成一个 bool。
 * - 枚举状态使用版本化码表，未知码保留原码并标记不确定。
 */
#pragma once

#include <common/core/identity.h>
#include <common/core/time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace ems::model {

/// 点种类：遥测/状态/事件/命令分离
enum class PointKind : std::uint8_t {
    Telemetry = 0,  ///< 遥测（模拟量）
    Status = 1,     ///< 状态（离散量）
    Event = 2,      ///< 事件（SOE）
    Command = 3     ///< 命令（遥控/遥调；反馈独立成点）
};

/// 点值类型（契约稳定枚举）
enum class ValueType : std::uint8_t { Bool = 0, Int64 = 1, UInt64 = 2, Double = 3, String = 4 };

/// 类型化点值：类型与值的组合由编码器校验
using PointValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

/// 质量稳定码表（契约的一部分；只增不改义，未知码保留并标记不确定）
enum class Quality : std::uint8_t {
    Good = 0,           ///< 正常
    Uncertain = 1,      ///< 不确定（如未知枚举码保留原码）
    Invalid = 2,        ///< 值无效
    Overflow = 3,       ///< 溢出
    OutOfRange = 4,     ///< 超出量程
    CommFailure = 5,    ///< 通信离线
    DeviceFailure = 6,  ///< 设备故障
    SensorFailure = 7,  ///< 传感器故障
    Stale = 8,          ///< 值陈旧（由采样时间+新鲜度期限计算）
    Unbound = 9,        ///< 配置未绑定
    Disabled = 10       ///< 配置禁用
};

/**
 * @brief 返回点值品质的稳定诊断名称。
 * @param[in] q 待提交的数据项。
 * @return 对应对象的指针。
 */
inline const char* to_string(Quality q) noexcept {
    switch (q) {
        case Quality::Good:
            return "Good";
        case Quality::Uncertain:
            return "Uncertain";
        case Quality::Invalid:
            return "Invalid";
        case Quality::Overflow:
            return "Overflow";
        case Quality::OutOfRange:
            return "OutOfRange";
        case Quality::CommFailure:
            return "CommFailure";
        case Quality::DeviceFailure:
            return "DeviceFailure";
        case Quality::SensorFailure:
            return "SensorFailure";
        case Quality::Stale:
            return "Stale";
        case Quality::Unbound:
            return "Unbound";
        case Quality::Disabled:
            return "Disabled";
    }
    return "Unknown";
}

/**
 * @brief 返回点值类型的稳定诊断名称。
 * @param[in] t 待转换的枚举值。
 * @return 对应对象的指针。
 */
inline const char* to_string(ValueType t) noexcept {
    switch (t) {
        case ValueType::Bool:
            return "Bool";
        case ValueType::Int64:
            return "Int64";
        case ValueType::UInt64:
            return "UInt64";
        case ValueType::Double:
            return "Double";
        case ValueType::String:
            return "String";
    }
    return "Unknown";
}

/**
 * @brief 返回点类别的稳定诊断名称。
 * @param[in] k 键或索引。
 * @return 对应对象的指针。
 */
inline const char* to_string(PointKind k) noexcept {
    switch (k) {
        case PointKind::Telemetry:
            return "Telemetry";
        case PointKind::Status:
            return "Status";
        case PointKind::Event:
            return "Event";
        case PointKind::Command:
            return "Command";
    }
    return "Unknown";
}

/// 质量信息：稳定码 + 可选原因
struct QualityInfo {
    Quality code{Quality::Invalid};
    std::string reason;  ///< 可选原因（脱敏诊断）
};

/// 标准点定义（设备类型目录中的标准语义）
struct StandardPointDefinition {
    std::string semantic_code;                ///< 标准语义编码，如 pcs.active_power
    ValueType value_type{ValueType::Double};  ///< 值类型
    std::string unit;                         ///< 单位
    PointKind kind{PointKind::Telemetry};     ///< 点种类
};

/// 点实例（挂在资产上；标准采样引用逻辑 point_id）
struct Point {
    core::PointId point_id;
    core::AssetId asset_id;  ///< 所属资产
    std::string code;        ///< 业务编码（格式/作用域引用 Redis 键专题，非显示名称）
    std::string name;        ///< 显示名称
    PointKind kind{PointKind::Telemetry};
    ValueType value_type{ValueType::Double};
    std::string unit;
    bool enabled{true};
    /// 命令反馈分点：反馈点指回对应命令点
    std::optional<core::PointId> feedback_of;
};

/// 类型化采样样本（遥测事件专属字段对应 design.md 10.1 telemetry 类别）
struct PointSample {
    std::optional<PointValue> value;  ///< nullopt = 无值
    QualityInfo quality;
    core::Sequence sequence{0};                            ///< 同一来源代次内递增
    core::SourceEpoch source_epoch;                        ///< 样本来源授权代次
    std::string source_id;                                 ///< 来源标识
    std::string config_version;                            ///< 产生该样本时已应用的配置版本
    core::UtcMicros source_time{core::kInvalidUtcMicros};  ///< 源时间
    core::UtcMicros ingest_time{core::kInvalidUtcMicros};  ///< 接收时间
};

/// 校验值与声明类型的组合是否匹配（编码器使用；字符串值禁止 NaN/Inf 混入）
/// @param[in] value 输入值。
/// @param[in] type 连接或数据类型。
/// @return true 表示操作成功；false 表示失败。
bool value_matches_type(const PointValue& value, ValueType type) noexcept;

}  // namespace ems::model
