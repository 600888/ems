/**
 * @file catalog.hpp
 * @brief DL/T 698.45—2017 常用标准对象、点位寻址和精确工程值。
 */
#pragma once
#include <dlt698/model/data.hpp>
#include <dlt698/standard/oi.hpp>
#include <optional>

namespace dlt698::standard {
/// 数组语义；非数组使用 none，数组顺序由接口类定义。
enum class ArrayLayout {
    none,
    phases,
    total_phases,
    total_tariffs,
    harmonics,
    status_words,
    variable
};
enum class Wiring { single_phase, three_phase };
enum class Phase { total, a, b, c };

/// 本地设备配置，不代表远端能力；单相按标准的 A（某一相）位置表示。
struct DeviceLayout {
    Wiring wiring = Wiring::three_phase;
    std::size_t tariff_count = 4;     ///< 费率数 0～254，不含总量；默认四费率须与设备配置一致。
    std::size_t harmonic_order = 21;  ///< 最高谐波次数 2～255，数组为总量及 2～n 次；须与设备一致。
};

/// 细化值定义：结构字段按协议顺序排列，size 对位串为位数，对字符串为字节数。
struct ValueDefinition {
    const char* name;
    model::DataType type;
    std::optional<model::ScalerUnit> scaling = {};
    std::optional<std::size_t> size = {};
    std::vector<ValueDefinition> fields = {};   ///< 结构字段，可递归嵌套；不附加时区或日历转换。
    std::optional<std::uint64_t> maximum = {};  ///< 无符号或枚举字段的标准上限。
    std::vector<std::uint64_t> allowed_values = {};  ///< 非空时限定无符号或枚举字段的允许值。
};

/// 单个属性的标准定义，运行时写权限仍由应用另行授权。
struct AttributeDefinition {
    std::uint8_t number;
    const char* name;
    model::DataType type;
    std::optional<model::DataType> element_type;
    ArrayLayout layout = ArrayLayout::none;
    std::optional<model::ScalerUnit> scaling;
    bool readable = true;
    bool writable = false;  ///< 标准写入能力；运行 schema 仍须由应用显式授权。
    bool record = false;
    const char* source = "";                                 ///< 标准章节/表号；字符串随目录存活。
    std::optional<ValueDefinition> element_definition = {};  ///< 数组元素的字段/长度约束。
    std::optional<ValueDefinition> value_definition = {};    ///< 非数组属性的字段/长度约束。
};

/// 常用 OI 定义；只列本批支持的属性，不声称覆盖整个接口类及其可选方法。
struct ObjectDefinition {
    std::uint16_t oi;
    const char* name;
    std::uint8_t class_id;
    const char* version;
    const char* source;
    std::vector<AttributeDefinition> attributes;
    std::optional<Phase> phase = {};  ///< 分相电能/需量的对象相别；单相配置不受理 B/C 对象。
};

/// 精确工程值：raw × 10^scaling.scaler；无默认浮点转换。
struct ScaledNumber {
    std::variant<std::int64_t, std::uint64_t> raw;
    model::ScalerUnit scaling;
};

/// 最大需量的精确数值及原始发生时间；两者均拥有自身数据。
struct DemandValue {
    ScaledNumber number;
    model::DateTimeS occurred_at;
};

/**
 * @brief 枚举已收录的不可变标准对象。
 * @return 进程生命周期内有效的常量引用；首次调用可能分配内存。
 * @note 查到定义不代表远端设备支持或本地已注册 provider；并发只读安全。
 */
DLT698_API const std::vector<ObjectDefinition>& objects();
/**
 * @brief 按 OI 查找本批标准定义。
 * @param[in] oi 对象标识，包括合法的 0x0000。
 * @return 不可变目录中的指针，进程生命周期内有效；未收录返回 nullptr。
 */
DLT698_API const ObjectDefinition* find_object(std::uint16_t oi);
/**
 * @brief 按 OI 和属性低五位查找定义，不验证特征和元素索引。
 * @param[in] attribute 保留完整线上字节的 OAD。
 * @return 进程生命周期内有效的定义指针；未收录返回 nullptr。
 */
DLT698_API const AttributeDefinition* find_attribute(const model::Oad& attribute);
/**
 * @brief 校验设备接线方式、费率数及最高谐波次数。
 * @param[in] layout 本地配置；费率数不超过 254，最高谐波次数为 2～255。
 * @return 成功或 invalid_value。
 */
DLT698_API Result<void> validate_layout(const DeviceLayout& layout);
/**
 * @brief 验证本批标准 OAD 的属性、特征和索引。
 * @param[in] attribute 原始 OAD；本批只支持特征零。
 * @param[in] layout 数组实例的接线方式和费率数。
 * @return 成功；未知定义为 unsupported_tag，非零特征为 unsupported_service，索引错误为 invalid_length，配置或不可用相别为 invalid_value。
 */
DLT698_API Result<void> validate_oad(const model::Oad& attribute, const DeviceLayout& layout = {});
/**
 * @brief 构造特征零的标准 OAD，不截断超范围编号或索引。
 * @param[in] oi 本批标准对象标识。
 * @param[in] attribute 属性编号 1～31。
 * @param[in] index 0 取整体；非零从 1 开始，选择数组元素或顶层结构字段。
 * @param[in] layout 本地数组实例配置。
 * @return 合法 OAD 或未知定义、配置/索引错误。
 */
DLT698_API Result<model::Oad> make_oad(std::uint16_t oi, std::size_t attribute = 2,
                                       std::size_t index = 0, const DeviceLayout& layout = {});
/**
 * @brief 构造电压、电流或功率类的相别点位。
 * @param[in] oi 本批分相或总及分相对象标识。
 * @param[in] phase 总量或 A/B/C；分相量没有总量，单相没有 B/C。
 * @param[in] layout 设备接线方式及费率数。
 * @return 属性 2 的元素 OAD，非法相别/对象返回 invalid_value。
 */
DLT698_API Result<model::Oad> phase_oad(std::uint16_t oi, Phase phase,
                                        const DeviceLayout& layout = {});
/**
 * @brief 构造电能或最大需量的总量/费率元素点位。
 * @param[in] oi 已收录的电能或最大需量对象标识。
 * @param[in] tariff 0 为总量，1～layout.tariff_count 为费率。
 * @param[in] layout 设备配置，费率数 0～254。
 * @param[in] high_precision true 使用电能属性 4，false 使用属性 2；最大需量不支持扩展精度。
 * @return 元素索引为 tariff+1 的 OAD；非法对象/费率返回错误。
 */
DLT698_API Result<model::Oad> tariff_oad(std::uint16_t oi, std::size_t tariff,
                                         const DeviceLayout& layout = {},
                                         bool high_precision = false);
/**
 * @brief 按电能对象家族、相别及费率构造元素 OAD。
 * @param[in] family 总电能的 OI，例如正向有功电能；组合有功仅支持 total。
 * @param[in] phase total 或 A/B/C；单相配置只支持 total/A。
 * @param[in] tariff 0 为总量，1～配置费率数为费率。
 * @param[in] layout 本地设备配置。
 * @param[in] high_precision 是否选择属性 4 的扩展精度电能。
 * @return 对应分相对象的 OAD；非法家族、相别、费率或配置返回错误。
 */
DLT698_API Result<model::Oad> energy_oad(std::uint16_t family, Phase phase, std::size_t tariff,
                                         const DeviceLayout& layout = {},
                                         bool high_precision = false);
/**
 * @brief 按最大需量对象家族、相别及费率构造结构元素 OAD。
 * @param[in] family 总最大需量的 OI，例如正向有功最大需量。
 * @param[in] phase total 或 A/B/C；单相配置只支持 total/A。
 * @param[in] tariff 0 为总量，1～配置费率数为费率。
 * @param[in] layout 本地设备配置。
 * @return 属性 2 的结构元素 OAD；非法家族、相别、费率或配置返回错误。
 */
DLT698_API Result<model::Oad> demand_oad(std::uint16_t family, Phase phase, std::size_t tariff,
                                         const DeviceLayout& layout = {});
/**
 * @brief 构造某相的谐波总含有量或指定次数含有量 OAD。
 * @param[in] oi 已收录的谐波含有量对象标识。
 * @param[in] phase A/B/C 对应属性 2/3/4；无 total 相，单相仅支持 A。
 * @param[in] order 0 取总含有量（索引 1）；2～配置最高次数取该次谐波（索引等于次数）。
 * @param[in] layout 本地设备配置，必须与设备属性 5 的最高次数一致。
 * @return 元素 OAD；非法对象、相别、次数或配置返回错误。
 */
DLT698_API Result<model::Oad> harmonic_oad(std::uint16_t oi, Phase phase, std::size_t order,
                                           const DeviceLayout& layout = {});
/**
 * @brief 校验并读取最大需量值及发生时间，整体数组按总量/费率顺序输出。
 * @param[in] attribute 最大需量属性 2 的整体或元素 OAD。
 * @param[in] value 原始 Data；结构内数值标签须与对象的有符号性一致。
 * @param[in] layout 本地设备配置。
 * @param[in] limits 校验资源上限。
 * @return 拥有内存的需量及原始 DateTimeS 列表；非最大需量属性为 unsupported_tag，其余错误按校验返回。
 */
DLT698_API Result<std::vector<DemandValue>> demand_values(const model::Oad& attribute,
                                                          const model::Data& value,
                                                          const DeviceLayout& layout = {},
                                                          const Limits& limits = {});
/**
 * @brief 校验标准值标签、结构字段、数组/位串/字符串长度和取值约束，以及资源限制。
 * @param[in] attribute 值对应的完整 OAD；索引非零时校验数组元素或顶层结构字段。
 * @param[in] value 拥有内存的原始值，调用期间借用；不执行浮点或日历转换。
 * @param[in] layout 数组长度由本地设备配置确定。
 * @param[in] limits 编码深度、元素和总字节限制。
 * @return 成功或 OAD/类型/长度/资源错误；不将 Data 错误伪装为测点不存在。
 * @note 记录型属性返回 unsupported_service，记录投影须用 validate_record_result 校验。
 */
DLT698_API Result<void> validate_value(const model::Oad& attribute, const model::Data& value,
                                       const DeviceLayout& layout = {}, const Limits& limits = {});
/**
 * @brief 使用 2017 标准默认倍率生成拥有内存的精确数值列表，按数组/结构字段顺序输出。
 * @param[in] attribute 对应数值属性或其元素的 OAD。
 * @param[in] value 原始 Data；函数不修改该值。
 * @param[in] layout 用于校验数组长度的设备配置。
 * @param[in] limits 校验资源上限。
 * @return 按数组及结构字段顺序输出带倍率的数值列表；没有数值倍率的属性返回 unsupported_tag。
 * @note 结构内无倍率的字段（如需量发生时间）不出现在数值列表；需要时间时使用 demand_values 或原始 Data。
 * @note 倍率来自目录；若设备单位属性与标准不一致，调用方须处理差异后再使用结果。
 */
DLT698_API Result<std::vector<ScaledNumber>> engineering_values(const model::Oad& attribute,
                                                                const model::Data& value,
                                                                const DeviceLayout& layout = {},
                                                                const Limits& limits = {});
/**
 * @brief 将整数及十进制倍率转为精确十进制文本，不使用浮点中间值。
 * @param[in] number 原始整数及有符号一字节倍率，不附加单位。
 * @return 拥有内存的十进制文本，保留倍率指定的小数位。
 */
DLT698_API std::string decimal_text(const ScaledNumber& number);
/**
 * @brief 显式转换为便于显示的近似浮点工程值。
 * @param[in] number 原始整数及倍率。
 * @return double 近似值；64 位整数及十进制小数可能损失精度。
 */
DLT698_API double approximate_value(const ScaledNumber& number);
/**
 * @brief 查询本批使用的附录 B 单位名称。
 * @param[in] unit 单位枚举代码；255 为无单位。
 * @return 指向静态字符串的 UTF-8 视图，进程生命周期内有效；未收录单位返回空视图。
 */
DLT698_API std::string_view unit_symbol(std::uint8_t unit);
}  // namespace dlt698::standard
