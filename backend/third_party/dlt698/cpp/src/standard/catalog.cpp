#include <algorithm>
#include <cmath>
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/standard/catalog.hpp>

namespace dlt698::standard {
namespace {
using model::DataType;

AttributeDefinition scalar(std::uint8_t number, const char* name, DataType type, const char* source,
                           std::optional<model::ScalerUnit> scaling = {}) {
    return {number, name, type, {}, ArrayLayout::none, scaling, true, false, false, source};
}

AttributeDefinition array(std::uint8_t number, const char* name, DataType element,
                          ArrayLayout layout, model::ScalerUnit scaling, const char* source) {
    return {number, name, DataType::array, element, layout, scaling, true, false, false, source};
}

ValueDefinition field(const char* name, DataType type,
                      std::optional<model::ScalerUnit> scaling = {},
                      std::optional<std::uint64_t> maximum = {}) {
    return {name, type, scaling, {}, {}, maximum, {}};
}

AttributeDefinition structured(std::uint8_t number, const char* name,
                               std::vector<ValueDefinition> fields, const char* source) {
    auto attribute = scalar(number, name, DataType::structure, source);
    attribute.value_definition =
        ValueDefinition{name, DataType::structure, {}, {}, std::move(fields), {}, {}};
    return attribute;
}

ObjectDefinition demand(std::uint16_t identifier, const char* name, bool signed_value,
                        std::uint8_t unit, Phase phase) {
    // 表 128：一级索引选择总/费率结构；结构内数值有倍率，发生时间保留 DateTimeS。
    auto value = scalar(2, "总及费率最大需量", DataType::array, "7.3.2 表 128；附录 E.2");
    value.element_type = DataType::structure;
    value.layout = ArrayLayout::total_tariffs;
    value.element_definition =
        ValueDefinition{"最大需量及发生时间",
                        DataType::structure,
                        {},
                        {},
                        {field("最大需量值", signed_value ? DataType::int32 : DataType::uint32,
                               model::ScalerUnit{-4, unit}),
                         field("发生时间", DataType::date_time_s)},
                        {},
                        {}};
    return {identifier,
            name,
            2,
            "DL/T 698.45-2017",
            "附录 E.2 表 E.2",
            {scalar(1, "逻辑名", DataType::octet_string, "7.3.2 表 128"), std::move(value),
             scalar(3, "换算及单位", DataType::scaler_unit, "7.3.2 表 128")},
            phase};
}

ObjectDefinition harmonic(std::uint16_t identifier, const char* name) {
    // 表 137 的属性 3 是 B 相数值组，换算及单位是属性 6；总量后紧接 2 次，无基波项。
    return {identifier,
            name,
            5,
            "DL/T 698.45-2017",
            "附录 E.3 表 E.3",
            {scalar(1, "逻辑名", DataType::octet_string, "7.3.5 表 137"),
             array(2, "A相总及2～n次含有量", DataType::int16, ArrayLayout::harmonics, {-2, 51},
                   "7.3.5 表 137；附录 E.3"),
             array(3, "B相总及2～n次含有量", DataType::int16, ArrayLayout::harmonics, {-2, 51},
                   "7.3.5 表 137；附录 E.3"),
             array(4, "C相总及2～n次含有量", DataType::int16, ArrayLayout::harmonics, {-2, 51},
                   "7.3.5 表 137；附录 E.3"),
             scalar(5, "最高谐波次数", DataType::uint8, "7.3.5 表 137"),
             scalar(6, "换算及单位", DataType::scaler_unit, "7.3.5 表 137")}};
}

ObjectDefinition parameter(std::uint16_t identifier, const char* name, DataType type, bool writable,
                           std::optional<std::size_t> size = {},
                           std::optional<model::ScalerUnit> scaling = {}) {
    auto value = scalar(2, name, type, "附录 E.5 表 E.5", scaling);
    value.writable = writable;
    if (size) value.value_definition = ValueDefinition{name, type, scaling, size, {}, {}, {}};
    return {identifier,
            name,
            8,
            "DL/T 698.45-2017",
            "附录 E.5 表 E.5",
            {scalar(1, "逻辑名", DataType::octet_string, "7.3.8 表 146"), std::move(value)}};
}

ObjectDefinition energy(std::uint16_t oi, const char* name, bool signed_value, std::uint8_t unit) {
    // 表 125：索引 1 是总量，之后是费率；附录 E.1 单独规定符号和精度。
    return {oi,
            name,
            1,
            "DL/T 698.45-2017",
            "附录 E.1 表 E.1",
            {scalar(1, "逻辑名", DataType::octet_string, "7.3.1 表 125"),
             array(2, "总及费率电能量", signed_value ? DataType::int32 : DataType::uint32,
                   ArrayLayout::total_tariffs, {-2, unit}, "7.3.1 表 125；附录 E.1"),
             scalar(3, "换算及单位", DataType::scaler_unit, "7.3.1 表 125"),
             array(4, "扩展精度总及费率电能量", signed_value ? DataType::int64 : DataType::uint64,
                   ArrayLayout::total_tariffs, {-4, unit}, "7.3.1 表 125；附录 E.1"),
             scalar(5, "扩展精度换算及单位", DataType::scaler_unit, "7.3.1 表 125")}};
}

ObjectDefinition variable(std::uint16_t oi, const char* name, std::uint8_t class_id, DataType type,
                          ArrayLayout layout, model::ScalerUnit scaling) {
    const char* source = class_id == 3   ? "7.3.3 表 130～131；附录 E.3"
                         : class_id == 4 ? "7.3.4 表 133～134；附录 E.3"
                                         : "7.3.6 表 139～140；附录 E.3";
    auto value = layout == ArrayLayout::none ? scalar(2, "数值", type, source, scaling)
                                             : array(2, "数值组", type, layout, scaling, source);
    ObjectDefinition object{oi,
                            name,
                            class_id,
                            "DL/T 698.45-2017",
                            "附录 E.3 表 E.3",
                            {scalar(1, "逻辑名", DataType::octet_string, source), std::move(value),
                             scalar(3, "换算及单位", DataType::scaler_unit, source)}};
    if (oi == standard::oi::current)
        object.attributes.push_back(
            scalar(4, "零线电流", DataType::int32, "附录 E.3 表 E.3", model::ScalerUnit{-3, 36}));
    return object;
}

std::size_t element_count(ArrayLayout array_layout, const DeviceLayout& layout) {
    // 单相功率依旧包含总量和 A 相两项；电能费率与相别数量无关。
    switch (array_layout) {
        case ArrayLayout::phases:
            return layout.wiring == Wiring::single_phase ? 1 : 3;
        case ArrayLayout::total_phases:
            return layout.wiring == Wiring::single_phase ? 2 : 4;
        case ArrayLayout::total_tariffs:
            return layout.tariff_count + 1;
        case ArrayLayout::harmonics:
            return layout.harmonic_order;
        case ArrayLayout::status_words:
            return 7;
        case ArrayLayout::variable:
            return 255;
        case ArrayLayout::none:
            return 0;
    }
    return 0;
}

ScaledNumber scaled(const model::Data& data, model::ScalerUnit scaling) {
    // 每一种标签先独立取出再提升整数宽度，UInt64 不经过有符号或浮点中间值。
    switch (data.type()) {
        case DataType::uint8:
            return {std::uint64_t{data.as<model::UInt8>().value}, scaling};
        case DataType::int16:
            return {std::int64_t{data.as<model::Int16>().value}, scaling};
        case DataType::int32:
            return {std::int64_t{data.as<model::Int32>().value}, scaling};
        case DataType::int64:
            return {data.as<model::Int64>().value, scaling};
        case DataType::uint16:
            return {std::uint64_t{data.as<model::UInt16>().value}, scaling};
        case DataType::uint32:
            return {std::uint64_t{data.as<model::UInt32>().value}, scaling};
        default:
            return {data.as<model::UInt64>().value, scaling};
    }
}

Result<void> validate_definition(const model::Data& data, const ValueDefinition& definition) {
    if (data.type() != definition.type)
        return Error{ErrorCode::invalid_value, 0, "standard field type"};
    if (definition.size) {
        std::size_t actual = 0;
        if (data.type() == DataType::bit_string)
            actual = data.as<model::BitString>().bit_count;
        else if (data.type() == DataType::visible_string)
            actual = data.as<model::VisibleString>().value.size();
        else if (data.type() == DataType::octet_string)
            actual = data.as<model::OctetString>().value.size();
        if (actual != *definition.size)
            return Error{ErrorCode::invalid_length, 0, "standard field size"};
    }
    if (data.type() == DataType::structure) {
        const auto& fields = data.as<model::Structure>().value;
        if (fields.size() != definition.fields.size())
            return Error{ErrorCode::invalid_length, 0, "standard structure fields"};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            auto valid = validate_definition(fields[i], definition.fields[i]);
            if (!valid) return valid;
        }
    }
    if (definition.maximum || !definition.allowed_values.empty()) {
        const auto number = data.type() == DataType::enumeration
                                ? std::uint64_t{data.as<model::Enum>().value}
                                : std::get<std::uint64_t>(scaled(data, {}).raw);
        if ((definition.maximum && number > *definition.maximum) ||
            (!definition.allowed_values.empty() &&
             std::find(definition.allowed_values.begin(), definition.allowed_values.end(),
                       number) == definition.allowed_values.end()))
            return Error{ErrorCode::invalid_value, 0, "standard field range"};
    }
    return {};
}

void collect_numbers(const model::Data& data, const ValueDefinition& definition,
                     std::vector<ScaledNumber>& result) {
    if (definition.scaling) result.push_back(scaled(data, *definition.scaling));
    if (data.type() == DataType::structure) {
        const auto& fields = data.as<model::Structure>().value;
        for (std::size_t i = 0; i < fields.size(); ++i)
            collect_numbers(fields[i], definition.fields[i], result);
    }
}
}  // namespace

const std::vector<ObjectDefinition>& objects() {
    // 完整元数据集中在此处，OI 编号复用公开常量；局部静态表的初始化由 C++ 保证并发安全。
    static const auto catalog = [] {
        std::vector<ObjectDefinition> result{
            energy(oi::combination_active_energy, "组合有功电能", true, 33),
            energy(oi::forward_active_energy, "正向有功电能", false, 33),
            energy(oi::reverse_active_energy, "反向有功电能", false, 33),
            energy(oi::combination_reactive_energy_1, "组合无功1电能", true, 35),
            energy(oi::combination_reactive_energy_2, "组合无功2电能", true, 35),
            variable(oi::voltage, "电压", 3, DataType::uint16, ArrayLayout::phases, {-1, 38}),
            variable(oi::current, "电流", 3, DataType::int32, ArrayLayout::phases, {-3, 36}),
            variable(oi::active_power, "有功功率", 4, DataType::int32, ArrayLayout::total_phases,
                     {-1, 27}),
            variable(oi::reactive_power, "无功功率", 4, DataType::int32, ArrayLayout::total_phases,
                     {-1, 31}),
            variable(oi::apparent_power, "视在功率", 4, DataType::int32, ArrayLayout::total_phases,
                     {-1, 29}),
            variable(oi::power_factor, "功率因数", 4, DataType::int16, ArrayLayout::total_phases,
                     {-3, 255}),
            variable(oi::frequency, "电网频率", 6, DataType::uint16, ArrayLayout::none, {-2, 47})};
        for (const auto identifier : {oi::date_time, oi::communication_address}) {
            auto value =
                scalar(2, identifier == oi::date_time ? "日期时间" : "通信地址",
                       identifier == oi::date_time ? DataType::date_time_s : DataType::octet_string,
                       "附录 E.5 表 E.5");
            value.writable = true;
            result.push_back(
                {identifier,
                 value.name,
                 8,
                 "DL/T 698.45-2017",
                 "附录 E.5 表 E.5",
                 {scalar(1, "逻辑名", DataType::octet_string, "7.3.8 表 145～146"), value}});
        }

        // 附录 E.1/E.2 中相别属于对象标识；各对象内部索引仍按总量、费率排列。
        result.push_back(
            demand(oi::forward_active_maximum_demand, "正向有功最大需量", false, 28, Phase::total));
        {
            auto object = energy(oi::forward_active_energy_a, "A相正向有功电能", false, 33);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_active_maximum_demand_a, "A相正向有功最大需量", false,
                                28, Phase::a));
        {
            auto object = energy(oi::forward_active_energy_b, "B相正向有功电能", false, 33);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_active_maximum_demand_b, "B相正向有功最大需量", false,
                                28, Phase::b));
        {
            auto object = energy(oi::forward_active_energy_c, "C相正向有功电能", false, 33);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_active_maximum_demand_c, "C相正向有功最大需量", false,
                                28, Phase::c));
        result.push_back(
            demand(oi::reverse_active_maximum_demand, "反向有功最大需量", false, 28, Phase::total));
        {
            auto object = energy(oi::reverse_active_energy_a, "A相反向有功电能", false, 33);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_active_maximum_demand_a, "A相反向有功最大需量", false,
                                28, Phase::a));
        {
            auto object = energy(oi::reverse_active_energy_b, "B相反向有功电能", false, 33);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_active_maximum_demand_b, "B相反向有功最大需量", false,
                                28, Phase::b));
        {
            auto object = energy(oi::reverse_active_energy_c, "C相反向有功电能", false, 33);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_active_maximum_demand_c, "C相反向有功最大需量", false,
                                28, Phase::c));
        result.push_back(demand(oi::combination_reactive_1_maximum_demand, "组合无功1最大需量",
                                true, 32, Phase::total));
        {
            auto object = energy(oi::combination_reactive_energy_1_a, "A相组合无功1电能", true, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_1_maximum_demand_a, "A相组合无功1最大需量",
                                true, 32, Phase::a));
        {
            auto object = energy(oi::combination_reactive_energy_1_b, "B相组合无功1电能", true, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_1_maximum_demand_b, "B相组合无功1最大需量",
                                true, 32, Phase::b));
        {
            auto object = energy(oi::combination_reactive_energy_1_c, "C相组合无功1电能", true, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_1_maximum_demand_c, "C相组合无功1最大需量",
                                true, 32, Phase::c));
        result.push_back(demand(oi::combination_reactive_2_maximum_demand, "组合无功2最大需量",
                                true, 32, Phase::total));
        {
            auto object = energy(oi::combination_reactive_energy_2_a, "A相组合无功2电能", true, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_2_maximum_demand_a, "A相组合无功2最大需量",
                                true, 32, Phase::a));
        {
            auto object = energy(oi::combination_reactive_energy_2_b, "B相组合无功2电能", true, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_2_maximum_demand_b, "B相组合无功2最大需量",
                                true, 32, Phase::b));
        {
            auto object = energy(oi::combination_reactive_energy_2_c, "C相组合无功2电能", true, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::combination_reactive_2_maximum_demand_c, "C相组合无功2最大需量",
                                true, 32, Phase::c));
        {
            auto object = energy(oi::quadrant_1_reactive_energy, "第一象限无功电能", false, 35);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_1_reactive_maximum_demand, "第一象限最大需量", false,
                                32, Phase::total));
        {
            auto object =
                energy(oi::quadrant_1_reactive_energy_a, "A相第一象限无功电能", false, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_1_reactive_maximum_demand_a, "A相第一象限最大需量",
                                false, 32, Phase::a));
        {
            auto object =
                energy(oi::quadrant_1_reactive_energy_b, "B相第一象限无功电能", false, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_1_reactive_maximum_demand_b, "B相第一象限最大需量",
                                false, 32, Phase::b));
        {
            auto object =
                energy(oi::quadrant_1_reactive_energy_c, "C相第一象限无功电能", false, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_1_reactive_maximum_demand_c, "C相第一象限最大需量",
                                false, 32, Phase::c));
        {
            auto object = energy(oi::quadrant_2_reactive_energy, "第二象限无功电能", false, 35);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_2_reactive_maximum_demand, "第二象限最大需量", false,
                                32, Phase::total));
        {
            auto object =
                energy(oi::quadrant_2_reactive_energy_a, "A相第二象限无功电能", false, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_2_reactive_maximum_demand_a, "A相第二象限最大需量",
                                false, 32, Phase::a));
        {
            auto object =
                energy(oi::quadrant_2_reactive_energy_b, "B相第二象限无功电能", false, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_2_reactive_maximum_demand_b, "B相第二象限最大需量",
                                false, 32, Phase::b));
        {
            auto object =
                energy(oi::quadrant_2_reactive_energy_c, "C相第二象限无功电能", false, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_2_reactive_maximum_demand_c, "C相第二象限最大需量",
                                false, 32, Phase::c));
        {
            auto object = energy(oi::quadrant_3_reactive_energy, "第三象限无功电能", false, 35);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_3_reactive_maximum_demand, "第三象限最大需量", false,
                                32, Phase::total));
        {
            auto object =
                energy(oi::quadrant_3_reactive_energy_a, "A相第三象限无功电能", false, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_3_reactive_maximum_demand_a, "A相第三象限最大需量",
                                false, 32, Phase::a));
        {
            auto object =
                energy(oi::quadrant_3_reactive_energy_b, "B相第三象限无功电能", false, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_3_reactive_maximum_demand_b, "B相第三象限最大需量",
                                false, 32, Phase::b));
        {
            auto object =
                energy(oi::quadrant_3_reactive_energy_c, "C相第三象限无功电能", false, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_3_reactive_maximum_demand_c, "C相第三象限最大需量",
                                false, 32, Phase::c));
        {
            auto object = energy(oi::quadrant_4_reactive_energy, "第四象限无功电能", false, 35);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_4_reactive_maximum_demand, "第四象限最大需量", false,
                                32, Phase::total));
        {
            auto object =
                energy(oi::quadrant_4_reactive_energy_a, "A相第四象限无功电能", false, 35);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_4_reactive_maximum_demand_a, "A相第四象限最大需量",
                                false, 32, Phase::a));
        {
            auto object =
                energy(oi::quadrant_4_reactive_energy_b, "B相第四象限无功电能", false, 35);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_4_reactive_maximum_demand_b, "B相第四象限最大需量",
                                false, 32, Phase::b));
        {
            auto object =
                energy(oi::quadrant_4_reactive_energy_c, "C相第四象限无功电能", false, 35);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::quadrant_4_reactive_maximum_demand_c, "C相第四象限最大需量",
                                false, 32, Phase::c));
        {
            auto object = energy(oi::forward_apparent_energy, "正向视在电能", false, 34);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_apparent_maximum_demand, "正向视在最大需量", false, 30,
                                Phase::total));
        {
            auto object = energy(oi::forward_apparent_energy_a, "A相正向视在电能", false, 34);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_apparent_maximum_demand_a, "A相正向视在最大需量", false,
                                30, Phase::a));
        {
            auto object = energy(oi::forward_apparent_energy_b, "B相正向视在电能", false, 34);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_apparent_maximum_demand_b, "B相正向视在最大需量", false,
                                30, Phase::b));
        {
            auto object = energy(oi::forward_apparent_energy_c, "C相正向视在电能", false, 34);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::forward_apparent_maximum_demand_c, "C相正向视在最大需量", false,
                                30, Phase::c));
        {
            auto object = energy(oi::reverse_apparent_energy, "反向视在电能", false, 34);
            object.phase = Phase::total;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_apparent_maximum_demand, "反向视在最大需量", false, 30,
                                Phase::total));
        {
            auto object = energy(oi::reverse_apparent_energy_a, "A相反向视在电能", false, 34);
            object.phase = Phase::a;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_apparent_maximum_demand_a, "A相反向视在最大需量", false,
                                30, Phase::a));
        {
            auto object = energy(oi::reverse_apparent_energy_b, "B相反向视在电能", false, 34);
            object.phase = Phase::b;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_apparent_maximum_demand_b, "B相反向视在最大需量", false,
                                30, Phase::b));
        {
            auto object = energy(oi::reverse_apparent_energy_c, "C相反向视在电能", false, 34);
            object.phase = Phase::c;
            result.push_back(std::move(object));
        }
        result.push_back(demand(oi::reverse_apparent_maximum_demand_c, "C相反向视在最大需量", false,
                                30, Phase::c));

        result.push_back(variable(oi::voltage_distortion, "电压波形失真度", 3, DataType::int16,
                                  ArrayLayout::phases, {-2, 51}));
        result.push_back(variable(oi::current_distortion, "电流波形失真度", 3, DataType::int16,
                                  ArrayLayout::phases, {-2, 51}));
        result.push_back(harmonic(oi::voltage_harmonics, "电压谐波含有量"));
        result.push_back(harmonic(oi::current_harmonics, "电流谐波含有量"));
        result.push_back(variable(oi::internal_temperature, "表内温度", 6, DataType::int16,
                                  ArrayLayout::none, {-1, 9}));
        result.push_back(variable(oi::current_active_demand, "当前有功需量", 6, DataType::int32,
                                  ArrayLayout::none, {-4, 28}));
        result.push_back(variable(oi::current_reactive_demand, "当前无功需量", 6, DataType::int32,
                                  ArrayLayout::none, {-4, 32}));
        result.push_back(variable(oi::current_apparent_demand, "当前视在需量", 6, DataType::int32,
                                  ArrayLayout::none, {-4, 30}));
        {
            auto value = scalar(2, "运行状态字1～7", DataType::array, "附录 E.3；附录 F.1～F.7");
            value.element_type = DataType::bit_string;
            value.layout = ArrayLayout::status_words;
            value.element_definition =
                ValueDefinition{"运行状态字", DataType::bit_string, {}, 16, {}, {}, {}};
            result.push_back(
                {oi::operating_status,
                 "电能表运行状态字",
                 6,
                 "DL/T 698.45-2017",
                 "附录 E.3 表 E.3；附录 F.1～F.7",
                 {scalar(1, "逻辑名", DataType::octet_string, "7.3.6 表 140"), std::move(value),
                  scalar(3, "换算及单位", DataType::scaler_unit, "7.3.6 表 140")}});
        }
        {
            auto value = scalar(2, "跟随上报状态字", DataType::bit_string, "附录 E.3；附录 F.14");
            value.value_definition =
                ValueDefinition{"跟随上报状态字", DataType::bit_string, {}, 32, {}, {}, {}};
            auto mode = value;
            mode.number = 4;
            mode.name = "跟随上报模式字";
            mode.value_definition->name = mode.name;
            mode.writable = true;
            result.push_back(
                {oi::follow_report_status,
                 "电能表跟随上报状态字",
                 6,
                 "DL/T 698.45-2017",
                 "附录 E.3 表 E.3；附录 F.14",
                 {scalar(1, "逻辑名", DataType::octet_string, "7.3.6 表 140"), std::move(value),
                  scalar(3, "换算及单位", DataType::scaler_unit, "7.3.6 表 140"),
                  std::move(mode)}});
        }
        result.push_back(parameter(oi::meter_number, "表号", DataType::octet_string, true));
        result.push_back(parameter(oi::customer_number, "客户编号", DataType::octet_string, true));
        result.push_back(parameter(oi::step_count, "阶梯数", DataType::uint8, true));
        result.push_back(
            parameter(oi::harmonic_analysis_order, "谐波分析次数", DataType::uint8, true));
        {
            auto object =
                parameter(oi::metering_element_count, "计量元件数", DataType::uint8, false);
            object.attributes[1].value_definition =
                ValueDefinition{"计量元件数", DataType::uint8, {}, {}, {}, {}, {1, 2, 3}};
            result.push_back(std::move(object));
        }
        result.push_back(
            parameter(oi::weekend_characteristic, "周休日特征字", DataType::bit_string, true, 8));
        result.push_back(parameter(oi::maximum_demand_period, "最大需量周期", DataType::uint8, true,
                                   {}, model::ScalerUnit{0, 6}));
        result.push_back(parameter(oi::sliding_interval, "滑差时间", DataType::uint8, true, {},
                                   model::ScalerUnit{0, 6}));
        result.push_back(
            parameter(oi::asset_code, "资产管理编码", DataType::visible_string, true, 32));
        result.push_back(
            parameter(oi::rated_voltage, "额定电压", DataType::visible_string, false, 6));
        result.push_back(
            parameter(oi::rated_current, "额定电流/基本电流", DataType::visible_string, false, 6));
        result.push_back(
            parameter(oi::maximum_current, "最大电流", DataType::visible_string, false, 6));
        result.push_back(parameter(oi::active_accuracy_class, "有功准确度等级",
                                   DataType::visible_string, false, 4));
        result.push_back(parameter(oi::reactive_accuracy_class, "无功准确度等级",
                                   DataType::visible_string, false, 4));
        result.push_back(
            parameter(oi::meter_model, "电能表型号", DataType::visible_string, false, 32));
        {
            auto object = parameter(oi::clock_source, "时钟源", DataType::structure, false);
            auto source = field("时钟源", DataType::enumeration, {}, 4);
            auto state = field("状态", DataType::enumeration, {}, 1);
            object.attributes[1] =
                structured(2, "时钟源", {std::move(source), std::move(state)}, "附录 E.5 表 E.5");
            result.push_back(std::move(object));
        }
        {
            auto object =
                parameter(oi::time_period_counts, "时区时段数", DataType::structure, true);
            object.attributes[1] = structured(2, "时区时段数",
                                              {field("年时区数", DataType::uint8, {}, 14),
                                               field("日时段表数", DataType::uint8, {}, 8),
                                               field("日时段数", DataType::uint8, {}, 14),
                                               field("费率数", DataType::uint8, {}, 63),
                                               field("公共假日数", DataType::uint8, {}, 254)},
                                              "附录 E.5 表 E.5");
            object.attributes[1].writable = true;
            result.push_back(std::move(object));
        }
        {
            auto object =
                parameter(oi::voltage_quality_limits, "电压合格率参数", DataType::structure, true);
            object.attributes[1] =
                structured(2, "电压合格率参数",
                           {field("电压考核上限", DataType::uint16, model::ScalerUnit{-1, 38}),
                            field("电压考核下限", DataType::uint16, model::ScalerUnit{-1, 38}),
                            field("电压合格上限", DataType::uint16, model::ScalerUnit{-1, 38}),
                            field("电压合格下限", DataType::uint16, model::ScalerUnit{-1, 38})},
                           "附录 E.5 表 E.5");
            object.attributes[1].writable = true;
            result.push_back(std::move(object));
        }
        // 4000 参数属性单独定义；元数据可写不代表运行 schema 已获得写权限。
        auto& date = *std::find_if(result.begin(), result.end(),
                                   [](const auto& object) { return object.oi == oi::date_time; });
        auto mode = scalar(3, "校时模式", DataType::enumeration, "附录 E.5 表 E.5");
        mode.writable = true;
        mode.value_definition =
            ValueDefinition{"校时模式", DataType::enumeration, {}, {}, {}, {}, {0, 1, 2, 255}};
        date.attributes.push_back(std::move(mode));
        auto timing = structured(
            4, "精准校时参数",
            {field("最近心跳总个数", DataType::uint8), field("最大值剔除个数", DataType::uint8),
             field("最小值剔除个数", DataType::uint8),
             field("通讯延时阈值", DataType::uint8, model::ScalerUnit{0, 7}),
             field("最少有效个数", DataType::uint8)},
            "附录 E.5 表 E.5");
        timing.writable = true;
        date.attributes.push_back(std::move(timing));
        // 记录型属性只声明入口，不将动态关联列伪造为固定普通数组。
        for (const auto identifier :
             {oi::daily_freeze, oi::monthly_freeze, oi::meter_power_down_event,
              oi::terminal_initialization_event}) {
            const bool freeze = identifier == oi::daily_freeze || identifier == oi::monthly_freeze;
            const char* name = identifier == oi::daily_freeze             ? "日冻结"
                               : identifier == oi::monthly_freeze         ? "月冻结"
                               : identifier == oi::meter_power_down_event ? "电能表掉电事件"
                                                                          : "终端初始化事件";
            const char* source = freeze ? "7.3.9 表 149；附录 E.6" : "7.3.7 表 143；附录 E.4";
            auto record = scalar(2, freeze ? "冻结数据表" : "事件记录表", DataType::array, source);
            record.record = true;
            record.element_type = DataType::structure;
            result.push_back(
                {identifier,
                 name,
                 static_cast<std::uint8_t>(freeze ? 9 : 7),
                 "DL/T 698.45-2017",
                 source,
                 {scalar(1, "逻辑名", DataType::octet_string, source), std::move(record)}});
        }
        for (const auto identifier : {oi::event_start_time, oi::event_end_time, oi::freeze_time,
                                      oi::event_sequence, oi::freeze_sequence}) {
            const char* name = identifier == oi::event_start_time ? "事件发生时间"
                               : identifier == oi::event_end_time ? "事件结束时间"
                               : identifier == oi::freeze_time    ? "数据冻结时间"
                               : identifier == oi::event_sequence ? "事件记录序号"
                                                                  : "冻结记录序号";
            const auto type = identifier == oi::event_sequence || identifier == oi::freeze_sequence
                                  ? DataType::uint32
                                  : DataType::date_time_s;
            result.push_back({identifier,
                              name,
                              8,
                              "DL/T 698.45-2017",
                              "附录 E.3 表 E.3",
                              {scalar(1, "逻辑名", DataType::octet_string, "附录 E.3 表 E.3"),
                               scalar(2, "数值", type, "附录 E.3 表 E.3")}});
        }
        // 表 184/185 与附录 E.12/E.13：安全对象与端口的值定义独立于 ESAM 密码后端。
        auto dynamic_array = [](std::uint8_t number, const char* name, DataType element,
                                const char* source) {
            auto attribute = scalar(number, name, DataType::array, source);
            attribute.element_type = element;
            attribute.layout = ArrayLayout::variable;
            return attribute;
        };
        std::vector<AttributeDefinition> esam_attributes;
        for (const auto& item :
             std::vector<std::pair<std::uint8_t, const char*>>{{std::uint8_t{1}, "逻辑名"},
                                                               {std::uint8_t{2}, "ESAM序列号"},
                                                               {std::uint8_t{3}, "ESAM版本号"},
                                                               {std::uint8_t{4}, "对称密钥版本"},
                                                               {std::uint8_t{9}, "终端证书序列号"},
                                                               {std::uint8_t{10}, "终端证书"},
                                                               {std::uint8_t{11}, "主站证书序列号"},
                                                               {std::uint8_t{12}, "主站证书"}})
            esam_attributes.push_back(
                scalar(item.first, item.second, DataType::octet_string, "7.3.21 表185"));
        for (const auto& item : std::vector<std::pair<std::uint8_t, const char*>>{
                 {std::uint8_t{5}, "会话时效门限"},
                 {std::uint8_t{6}, "会话时效剩余时间"},
                 {std::uint8_t{14}, "红外认证时效门限"},
                 {std::uint8_t{15}, "红外认证剩余时间"}})
            esam_attributes.push_back(scalar(item.first, item.second, DataType::uint32,
                                             "7.3.21 表185",
                                             model::ScalerUnit{std::uint8_t{0}, 6}));
        esam_attributes.push_back(structured(7, "当前计数器",
                                             {field("单地址应用协商计数器", DataType::uint32),
                                              field("主动上报计数器", DataType::uint32),
                                              field("应用广播通信序列号", DataType::uint32)},
                                             "7.3.21 表185"));
        esam_attributes.push_back(structured(8, "证书版本",
                                             {field("终端证书版本", DataType::octet_string),
                                              field("主站证书版本", DataType::octet_string)},
                                             "7.3.21 表185"));
        esam_attributes.push_back(
            dynamic_array(13, "ESAM安全存储对象列表", DataType::oad, "7.3.21 表185"));
        result.push_back(
            {oi::esam, "ESAM", 21, "DL/T 698.45-2017", "附录 E.12", std::move(esam_attributes)});
        auto enabled = scalar(2, "安全模式选择", DataType::enumeration, "附录 E.12");
        enabled.value_definition = field(enabled.name, DataType::enumeration, {}, 1);
        auto sal = enabled;
        sal.number = 4;
        sal.name = "SAL安全应用数据链路层参数";
        auto modes = dynamic_array(3, "显式安全模式参数", DataType::structure, "附录 E.12");
        modes.element_definition =
            ValueDefinition{modes.name,
                            DataType::structure,
                            {},
                            {},
                            {field("对象标识", DataType::oi), field("安全模式", DataType::uint16)},
                            {},
                            {}};
        result.push_back(
            {oi::security_mode,
             "安全模式参数",
             8,
             "DL/T 698.45-2017",
             "附录 E.12",
             {scalar(1, "逻辑名", DataType::octet_string, "附录 E.12"), enabled, modes, sal}});
        for (const auto& port : std::vector<std::pair<std::uint16_t, const char*>>{
                 {oi::rs232, "RS-232"}, {oi::rs485, "RS-485"}}) {
            auto ports = dynamic_array(2, "设备对象列表", DataType::structure, "附录 E.13");
            auto function = field("端口功能", DataType::enumeration);
            function.allowed_values = {0, 1, 3};
            ports.element_definition =
                ValueDefinition{ports.name,
                                DataType::structure,
                                {},
                                {},
                                {field("端口描述符", DataType::visible_string),
                                 field("端口参数", DataType::comdcb), function},
                                {},
                                {}};
            result.push_back({port.first,
                              port.second,
                              22,
                              "DL/T 698.45-2017",
                              "附录 E.13",
                              {scalar(1, "逻辑名", DataType::octet_string, "附录 E.13"), ports}});
        }
        return result;
    }();
    return catalog;
}

const ObjectDefinition* find_object(std::uint16_t oi) {
    const auto& catalog = objects();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [oi](const auto& object) { return object.oi == oi; });
    return it == catalog.end() ? nullptr : &*it;
}

const AttributeDefinition* find_attribute(const model::Oad& attribute) {
    const auto object = find_object(attribute.oi);
    if (!object) return nullptr;
    const auto it =
        std::find_if(object->attributes.begin(), object->attributes.end(),
                     [&](const auto& entry) { return entry.number == (attribute.attribute & 31); });
    return it == object->attributes.end() ? nullptr : &*it;
}

Result<void> validate_layout(const DeviceLayout& layout) {
    if ((layout.wiring != Wiring::single_phase && layout.wiring != Wiring::three_phase) ||
        layout.tariff_count > 254 || layout.harmonic_order < 2 || layout.harmonic_order > 255)
        return Error{ErrorCode::invalid_value, 0, "standard device layout"};
    return {};
}

Result<void> validate_oad(const model::Oad& attribute, const DeviceLayout& layout) {
    auto config = validate_layout(layout);
    if (!config) return config;
    const auto definition = find_attribute(attribute);
    if (!definition) return Error{ErrorCode::unsupported_tag, 0, "standard OI/attribute"};
    // schema 查低五位，特征位原样保留；本批不对快照特征伪造语义。
    if (attribute.attribute & 0xe0)
        return Error{ErrorCode::unsupported_service, 0, "standard attribute feature"};
    const auto object = find_object(attribute.oi);
    const auto number = attribute.attribute & 31;
    if (layout.wiring == Wiring::single_phase &&
        ((object->phase && (*object->phase == Phase::b || *object->phase == Phase::c)) ||
         (object->class_id == 5 && (number == 3 || number == 4))))
        return Error{ErrorCode::invalid_value, 0, "standard phase unavailable"};
    // 一级索引选数组元素或顶层结构字段，不递归解释需量结构内的第二级字段。
    const auto count = definition->type == DataType::structure && definition->value_definition
                           ? definition->value_definition->fields.size()
                           : element_count(definition->layout, layout);
    if (attribute.index && attribute.index > count)
        return Error{ErrorCode::invalid_length, 0, "standard element index"};
    return {};
}

Result<model::Oad> make_oad(std::uint16_t oi, std::size_t attribute, std::size_t index,
                            const DeviceLayout& layout) {
    if (!attribute || attribute > 31 || index > 255)
        return Error{ErrorCode::invalid_value, 0, "standard OAD fields"};
    const model::Oad oad{oi, static_cast<std::uint8_t>(attribute),
                         static_cast<std::uint8_t>(index)};
    auto valid = validate_oad(oad, layout);
    if (!valid) return valid.error();
    return oad;
}

Result<model::Oad> phase_oad(std::uint16_t oi, Phase phase, const DeviceLayout& layout) {
    const auto definition = find_attribute({oi, 2, 0});
    if (!definition ||
        (definition->layout != ArrayLayout::phases &&
         definition->layout != ArrayLayout::total_phases) ||
        phase < Phase::total || phase > Phase::c ||
        (phase == Phase::total && definition->layout == ArrayLayout::phases) ||
        (layout.wiring == Wiring::single_phase && (phase == Phase::b || phase == Phase::c)))
        return Error{ErrorCode::invalid_value, 0, "standard phase selection"};
    const auto index =
        static_cast<std::size_t>(phase) + (definition->layout == ArrayLayout::total_phases ? 1 : 0);
    return make_oad(oi, 2, index, layout);
}

Result<model::Oad> tariff_oad(std::uint16_t oi, std::size_t tariff, const DeviceLayout& layout,
                              bool high_precision) {
    auto config = validate_layout(layout);
    if (!config) return config.error();
    const std::uint8_t attribute = high_precision ? 4 : 2;
    const auto definition = find_attribute({oi, attribute, 0});
    if (!definition || definition->layout != ArrayLayout::total_tariffs ||
        tariff > layout.tariff_count)
        return Error{ErrorCode::invalid_value, 0, "standard tariff selection"};
    return make_oad(oi, attribute, tariff + 1, layout);
}

namespace {
Result<model::Oad> family_oad(std::uint16_t family, Phase phase, std::size_t tariff,
                              const DeviceLayout& layout, bool high_precision,
                              std::uint8_t class_id) {
    const auto object = find_object(family);
    if (!object || object->class_id != class_id || (family & 15) || phase < Phase::total ||
        phase > Phase::c)
        return Error{ErrorCode::invalid_value, 0, "standard object family"};
    // 仅在已收录家族内按标准相别偏移寻址，查询候选对象防止生成不存在的 OI。
    const auto identifier = static_cast<std::uint16_t>(family + static_cast<unsigned>(phase));
    const auto selected = find_object(identifier);
    if (!selected || selected->class_id != class_id ||
        (phase != Phase::total && selected->phase != phase))
        return Error{ErrorCode::invalid_value, 0, "standard phase object"};
    return tariff_oad(identifier, tariff, layout, high_precision);
}
}  // namespace

Result<model::Oad> energy_oad(std::uint16_t family, Phase phase, std::size_t tariff,
                              const DeviceLayout& layout, bool high_precision) {
    return family_oad(family, phase, tariff, layout, high_precision, 1);
}

Result<model::Oad> demand_oad(std::uint16_t family, Phase phase, std::size_t tariff,
                              const DeviceLayout& layout) {
    return family_oad(family, phase, tariff, layout, false, 2);
}

Result<model::Oad> harmonic_oad(std::uint16_t identifier, Phase phase, std::size_t order,
                                const DeviceLayout& layout) {
    const auto object = find_object(identifier);
    if (!object || object->class_id != 5 || phase < Phase::a || phase > Phase::c || order == 1 ||
        order > layout.harmonic_order)
        return Error{ErrorCode::invalid_value, 0, "standard harmonic selection"};
    return make_oad(identifier, static_cast<std::size_t>(phase) + 1, order ? order : 1, layout);
}

Result<void> validate_value(const model::Oad& attribute, const model::Data& value,
                            const DeviceLayout& layout, const Limits& limits) {
    auto valid = validate_oad(attribute, layout);
    if (!valid) return valid;
    const auto definition = find_attribute(attribute);
    if (definition->record)
        return Error{ErrorCode::unsupported_service, 0, "record value requires record validation"};
    const ValueDefinition* detail =
        definition->value_definition ? &*definition->value_definition : nullptr;
    const auto field_index = attribute.index && definition->type == DataType::structure;
    if (field_index) detail = &detail->fields[attribute.index - 1];
    if (attribute.index && definition->type == DataType::array)
        detail = definition->element_definition ? &*definition->element_definition : nullptr;
    const auto expected = field_index       ? detail->type
                          : attribute.index ? *definition->element_type
                                            : definition->type;
    if (value.type() != expected) return Error{ErrorCode::invalid_value, 0, "standard Data type"};
    // 先限制整棵 Data 树，再递归检查标准结构；超深、超节点值不会进入结构遍历。
    auto encoded = codec::encode_data(value, limits);
    if (!encoded) return encoded.error();
    if (!attribute.index && definition->type == DataType::array) {
        const auto& elements = value.as<model::Array>().value;
        if (elements.size() > limits.max_elements)
            return Error{ErrorCode::resource_limit, 0, "standard array elements"};
        if (definition->layout != ArrayLayout::variable &&
            elements.size() != element_count(definition->layout, layout))
            return Error{ErrorCode::invalid_length, 0, "standard array layout"};
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (elements[i].type() != *definition->element_type)
                return Error{ErrorCode::invalid_value, i, "standard array element type"};
            if (definition->element_definition) {
                auto valid_element =
                    validate_definition(elements[i], *definition->element_definition);
                if (!valid_element) return valid_element;
            }
        }
    }
    if (detail) return validate_definition(value, *detail);
    return {};
}

Result<std::vector<ScaledNumber>> engineering_values(const model::Oad& attribute,
                                                     const model::Data& value,
                                                     const DeviceLayout& layout,
                                                     const Limits& limits) {
    auto valid = validate_value(attribute, value, layout, limits);
    if (!valid) return valid.error();
    const auto definition = find_attribute(attribute);
    std::vector<ScaledNumber> result;
    auto collect = [&](const model::Data& data, const ValueDefinition* detail) {
        if (detail)
            collect_numbers(data, *detail, result);
        else if (definition->scaling)
            result.push_back(scaled(data, *definition->scaling));
    };
    if (value.type() == DataType::array) {
        const auto& elements = value.as<model::Array>().value;
        result.reserve(elements.size());
        for (const auto& element : elements)
            collect(element,
                    definition->element_definition ? &*definition->element_definition : nullptr);
    } else {
        const ValueDefinition* detail = nullptr;
        if (attribute.index && definition->type == DataType::array)
            detail = definition->element_definition ? &*definition->element_definition : nullptr;
        else if (definition->value_definition)
            detail = attribute.index ? &definition->value_definition->fields[attribute.index - 1]
                                     : &*definition->value_definition;
        collect(value, detail);
    }
    if (result.empty())
        return Error{ErrorCode::unsupported_tag, 0, "non-numeric standard attribute"};
    return result;
}

Result<std::vector<DemandValue>> demand_values(const model::Oad& attribute,
                                               const model::Data& value, const DeviceLayout& layout,
                                               const Limits& limits) {
    const auto object = find_object(attribute.oi);
    if (!object || object->class_id != 2 || (attribute.attribute & 31) != 2)
        return Error{ErrorCode::unsupported_tag, 0, "non-demand attribute"};
    auto valid = validate_value(attribute, value, layout, limits);
    if (!valid) return valid.error();
    const auto& definition = *find_attribute(attribute)->element_definition;
    std::vector<DemandValue> result;
    auto append = [&](const model::Data& data) {
        const auto& fields = data.as<model::Structure>().value;
        result.push_back(
            {scaled(fields[0], *definition.fields[0].scaling), fields[1].as<model::DateTimeS>()});
    };
    if (value.type() == DataType::array) {
        result.reserve(value.as<model::Array>().value.size());
        for (const auto& element : value.as<model::Array>().value) append(element);
    } else
        append(value);
    return result;
}

std::string decimal_text(const ScaledNumber& number) {
    auto text = std::visit([](auto raw) { return std::to_string(raw); }, number.raw);
    const bool negative = text.front() == '-';
    if (negative) text.erase(0, 1);
    const int scaler = number.scaling.scaler;
    if (scaler >= 0) {
        if (text != "0") text.append(static_cast<std::size_t>(scaler), '0');
    } else {
        // 直接插入小数点；先提升到 int 再取负，避免 -128 的一字节溢出。
        const auto places = static_cast<std::size_t>(-scaler);
        if (text.size() <= places) text.insert(0, places + 1 - text.size(), '0');
        text.insert(text.size() - places, 1, '.');
    }
    if (negative) text.insert(0, 1, '-');
    return text;
}

double approximate_value(const ScaledNumber& number) {
    return std::visit([](auto raw) { return static_cast<double>(raw); }, number.raw) *
           std::pow(10.0, static_cast<int>(number.scaling.scaler));
}

std::string_view unit_symbol(std::uint8_t unit) {
    switch (unit) {
        case 6:
            return "min";
        case 7:
            return "s";
        case 9:
            return "℃";
        case 27:
            return "W";
        case 28:
            return "kW";
        case 29:
            return "VA";
        case 30:
            return "kVA";
        case 31:
            return "var";
        case 32:
            return "kvar";
        case 33:
            return "kWh";
        case 34:
            return "kVAh";
        case 35:
            return "kvarh";
        case 36:
            return "A";
        case 38:
            return "V";
        case 47:
            return "Hz";
        case 51:
            return "%";
        case 255:
            return "";
        default:
            return "";
    }
}
}  // namespace dlt698::standard
