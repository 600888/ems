/**
 * @file point.h
 * @brief 测点模型：四遥点定义（遥测/遥信/遥控/遥调）、类型化样本与质量码
 *
 * - PointValue 使用 variant<bool,int64,uint64,double,string>，外层 optional 表示无值；
 *   禁止将所有值转 double，禁止 NaN/Inf。
 * - 四遥是点的权威分类，四类点各有独立结构体，字段按类型拆分而不是共用可空列：
 *   YcPoint（倍率/量程/死区）、YxPoint（变位/SOE/反转）、
 *   YkPoint（固定值/脉冲）、YtPoint（工程值换算/量程）。
 * - PointKind 只描述实时记录的存储种类，由 PointType 映射：遥控与遥调都存为
 *   Command，运行侧必须读 point_type 才能区分“固定值写入”与“缩放写入”。
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

/// 点种类：遥测/状态/事件/命令分离（实时记录的存储种类）
enum class PointKind : std::uint8_t {
    Telemetry = 0,  ///< 遥测（模拟量）
    Status = 1,     ///< 状态（离散量）
    Event = 2,      ///< 事件（SOE）
    Command = 3     ///< 命令（遥控/遥调；反馈独立成点）
};

/**
 * @brief 电力四遥分类（配置与运行共用的权威分类）。
 *
 * 遥测/遥信是上行采集，遥控/遥调是下行写入。遥控按固定值或直接值写入，
 * 遥调按工程值换算后写入，两者都映射到 PointKind::Command，因此必须单独保存
 * point_type，否则运行侧无法区分“开机=1”与“功率设定 82.5 kW”。
 *
 * 注意：旧模型层 `src/models/base.h` 另有同名枚举（YC/YX/YK/YT），语义一致，
 * 但迁移期不得在同一编译单元同时包含两个头文件。
 */
enum class PointType : std::uint8_t {
    YC = 0,  ///< 遥测（模拟量上行）
    YX = 1,  ///< 遥信（离散量上行，含 SOE）
    YK = 2,  ///< 遥控（离散量下行）
    YT = 3   ///< 遥调（模拟量下行设定）
};

/// 命令写入方式：遥控固定值、直接值、遥调工程值换算
enum class WriteMode : std::uint8_t {
    Direct = 0,  ///< 直接写入调用方给出的原码
    Fixed = 1,   ///< 固定写入值（开机=1、关机=2）
    Scaled = 2   ///< 工程值按倍率换算为原码（遥调）
};

/**
 * @brief 返回四遥分类的稳定诊断名称。
 * @param[in] t 四遥分类。
 * @return 对应对象的指针，如 "YC"；未知值返回 "Unknown"。
 */
inline const char* to_string(PointType t) noexcept {
    switch (t) {
        case PointType::YC:
            return "YC";
        case PointType::YX:
            return "YX";
        case PointType::YK:
            return "YK";
        case PointType::YT:
            return "YT";
    }
    return "Unknown";
}

/**
 * @brief 返回命令写入方式的稳定诊断名称。
 * @param[in] mode 写入方式。
 * @return 对应对象的指针；未知值返回 "Unknown"。
 */
inline const char* to_string(WriteMode mode) noexcept {
    switch (mode) {
        case WriteMode::Direct:
            return "Direct";
        case WriteMode::Fixed:
            return "Fixed";
        case WriteMode::Scaled:
            return "Scaled";
    }
    return "Unknown";
}

/**
 * @brief 把四遥分类映射为实时记录种类。
 * @param[in] t 四遥分类。
 * @return 记录种类；遥控与遥调都返回 Command。
 */
constexpr PointKind to_point_kind(PointType t) noexcept {
    switch (t) {
        case PointType::YC:
            return PointKind::Telemetry;
        case PointType::YX:
            return PointKind::Status;
        case PointType::YK:
        case PointType::YT:
            return PointKind::Command;
    }
    return PointKind::Telemetry;
}

/**
 * @brief 判断四遥分类是否为下行命令（遥控或遥调）。
 * @param[in] t 四遥分类。
 * @return true 表示需要写入规则与反馈判据。
 */
constexpr bool is_command(PointType t) noexcept { return t == PointType::YK || t == PointType::YT; }

/**
 * @brief 判断四遥分类是否为上行采集量（遥测或遥信）。
 * @param[in] t 四遥分类。
 * @return true 表示由采样写入。
 */
constexpr bool is_measured(PointType t) noexcept {
    return t == PointType::YC || t == PointType::YX;
}

/**
 * @brief 判断四遥分类与实时记录种类是否自洽。
 *
 * 允许 Event 与 YX 组合：SOE 变位记录沿用事件记录种类，但调度语义仍属遥信。
 *
 * @param[in] type 四遥分类。
 * @param[in] kind 记录种类。
 * @return true 表示组合合法。
 */
constexpr bool point_type_matches_kind(PointType type, PointKind kind) noexcept {
    if (type == PointType::YX) return kind == PointKind::Status || kind == PointKind::Event;
    return to_point_kind(type) == kind;
}

/**
 * @brief 由记录种类推导四遥分类（仅用于旧快照兼容）。
 *
 * 旧快照没有 point_type 时按 kind 推导；Command 只能推出 YK，遥调需要重新发布
 * 才能恢复，因此新配置必须显式写入 point_type。
 *
 * @param[in] kind 记录种类。
 * @return 推导出的四遥分类。
 */
constexpr PointType derive_point_type(PointKind kind) noexcept {
    switch (kind) {
        case PointKind::Telemetry:
            return PointType::YC;
        case PointKind::Status:
        case PointKind::Event:
            return PointType::YX;
        case PointKind::Command:
            return PointType::YK;
    }
    return PointType::YC;
}

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

/// 类型内固有点定义（设备类型目录中的标准语义）
struct StandardPointDefinition {
    std::string semantic_code;                ///< 类型内标准语义编码，如 active_power
    ValueType value_type{ValueType::Double};  ///< 值类型
    std::string unit;                         ///< 单位
    PointKind kind{PointKind::Telemetry};     ///< 记录种类，由 point_type 映射
    PointType point_type{PointType::YC};      ///< 四遥分类；必须与 kind 自洽
};

/**
 * @brief 四遥点的公共字段。
 *
 * 构造函数为 protected：只能通过四个具体类型创建，保证 point_type / kind /
 * value_type 三者始终自洽，避免出现“遥测点标记可写”这类组合。
 */
struct PointBase {
    core::PointId point_id;                   ///< 稳定点 ID
    core::AssetId asset_id;                   ///< 所属设备节点；部件节点的点由上位通信设备采集
    std::string code;                         ///< 业务编码（非显示名称）
    std::string name;                         ///< 显示名称
    PointType point_type{PointType::YC};      ///< 四遥分类
    PointKind kind{PointKind::Telemetry};     ///< 记录种类，由 point_type 映射
    ValueType value_type{ValueType::Double};  ///< 值类型
    std::string unit;                         ///< 工程单位
    bool enabled{true};                       ///< 配置启用
    /// 反馈点指回命令点；命令点自身不填
    std::optional<core::PointId> feedback_of;

   protected:
    /**
     * @brief 由具体四遥类型调用，固定分类与记录种类。
     * @param[in] type 四遥分类。
     * @param[in] kind 记录种类。
     * @param[in] value_type 默认值类型。
     */
    PointBase(PointType type, PointKind kind, ValueType value_type) noexcept
        : point_type(type), kind(kind), value_type(value_type) {}
};

/// 遥测点：模拟量上行，带倍率、量程与死区
struct YcPoint : PointBase {
    YcPoint() noexcept : PointBase(PointType::YC, PointKind::Telemetry, ValueType::Double) {}

    double scale_factor{1.0};      ///< 标准值 = 原始值 × scale + offset
    double offset_value{0.0};      ///< 偏移
    double range_min{-9999999.0};  ///< 量程下限（工程值）
    double range_max{9999999.0};   ///< 量程上限（工程值）
    double deadband{0.0};          ///< 变化死区，0 表示逐点上报
};

/// 遥信点：离散量上行，含变位记录与取反
struct YxPoint : PointBase {
    YxPoint() noexcept : PointBase(PointType::YX, PointKind::Status, ValueType::Bool) {}

    bool soe{false};     ///< 是否记录变位（SOE），仍属遥信，不单列事件种类
    bool invert{false};  ///< 原始位取反（常闭接点）
};

/// 遥控点：离散量下行，固定值/直接值写入，可脉冲
struct YkPoint : PointBase {
    YkPoint() noexcept : PointBase(PointType::YK, PointKind::Command, ValueType::Bool) {}

    WriteMode write_mode{WriteMode::Direct};      ///< 写入方式
    std::optional<std::int64_t> fixed_raw_value;  ///< Fixed 模式必填
    std::uint32_t pulse_width_ms{0};              ///< 脉冲宽度，0 表示保持
    std::uint32_t min_interval_ms{0};             ///< 最小指令间隔
    std::string write_group_key;                  ///< 共享写寄存器组，同组互斥
    std::uint32_t feedback_timeout_ms{5000};      ///< 反馈超时
};

/// 遥调点：模拟量下行设定，工程值按倍率换算
struct YtPoint : PointBase {
    YtPoint() noexcept : PointBase(PointType::YT, PointKind::Command, ValueType::Double) {}

    double scale_factor{1.0};                 ///< 原码 = (工程值 - offset) / scale
    double offset_value{0.0};                 ///< 偏移
    double range_min{-9999999.0};             ///< 可写下限（工程值）
    double range_max{9999999.0};              ///< 可写上限（工程值）
    std::uint32_t min_interval_ms{0};         ///< 最小调节间隔
    std::string write_group_key;              ///< 共享写寄存器组，同组互斥
    std::uint32_t feedback_timeout_ms{5000};  ///< 反馈超时
};

/// 四遥点：四种具体类型的封闭集合
using AnyPoint = std::variant<YcPoint, YxPoint, YkPoint, YtPoint>;

/**
 * @brief 按四遥分类创建一个默认点实例。
 * @param[in] type 四遥分类。
 * @return 对应类型的点；未知分类按遥测返回。
 */
inline AnyPoint make_point(PointType type) {
    switch (type) {
        case PointType::YC:
            return YcPoint{};
        case PointType::YX:
            return YxPoint{};
        case PointType::YK:
            return YkPoint{};
        case PointType::YT:
            return YtPoint{};
    }
    return YcPoint{};
}

/**
 * @brief 按类型目录的固有点定义创建一个点实例。
 * @param[in] definition 固有点定义（提供四遥分类、值类型、单位）。
 * @param[in] point_id 稳定点 ID。
 * @param[in] asset_id 所属设备节点 ID。
 * @param[in] code 业务编码。
 * @return 已填好身份与类型字段的点实例。
 */
inline AnyPoint make_standard_point(const StandardPointDefinition& definition,
                                    core::PointId point_id, core::AssetId asset_id,
                                    std::string code) {
    AnyPoint point = make_point(definition.point_type);
    std::visit(
        [&](auto& p) {
            p.point_id = std::move(point_id);
            p.asset_id = std::move(asset_id);
            p.code = std::move(code);
            p.name = definition.semantic_code;
            p.value_type = definition.value_type;
            p.unit = definition.unit;
            p.kind = to_point_kind(definition.point_type);
        },
        point);
    return point;
}

/**
 * @brief 以公共基类引用访问任意四遥点。
 * @param[in] point 四遥点。
 * @return 公共字段视图。
 */
inline const PointBase& as_base(const AnyPoint& point) {
    return std::visit([](const auto& p) -> const PointBase& { return p; }, point);
}

/**
 * @brief 以公共基类引用访问任意四遥点（可写）。
 * @param[in,out] point 四遥点。
 * @return 公共字段视图。
 */
inline PointBase& as_base(AnyPoint& point) {
    return std::visit([](auto& p) -> PointBase& { return p; }, point);
}

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
    std::string event_id;  ///< 事件/命令受理 ID；普通周期采样可为空，重试保持不变
};

/// 校验值与声明类型的组合是否匹配（编码器使用；字符串值禁止 NaN/Inf 混入）
/// @param[in] value 输入值。
/// @param[in] type 连接或数据类型。
/// @return true 表示操作成功；false 表示失败。
bool value_matches_type(const PointValue& value, ValueType type) noexcept;

}  // namespace ems::model
