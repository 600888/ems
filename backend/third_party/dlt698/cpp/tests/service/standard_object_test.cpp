/**
 * @file standard_object_test.cpp
 * @brief 标准点位只读 schema 生成、目录一致性与严格 provider 绑定的单元测试。
 */
#include <dlt698/service/standard_object.hpp>
#include <dlt698/standard/catalog.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::service;
namespace oi = standard::oi;
namespace apdu = protocol::apdu;
using apdu::RecordResult;

namespace {

/// 断言读取结果是 DAR 并返回原码。
std::uint8_t dar_of(const ObjectValue& value) {
    REQUIRE(std::holds_alternative<std::uint8_t>(value));
    return std::get<std::uint8_t>(value);
}

/// 断言记录结果是 DAR 并返回原码。
std::uint8_t record_dar_of(const RecordResult& result) {
    REQUIRE(std::holds_alternative<std::uint8_t>(result.result));
    return std::get<std::uint8_t>(result.result);
}

/// 按属性编号取出已注册 schema 中的属性定义。
const AttributeSchema* attribute_of(const ObjectSchema& schema, std::uint8_t number) {
    for (const auto& attribute : schema.attributes)
        if (attribute.number == number) return &attribute;
    return nullptr;
}

/**
 * @brief 独立于实现的目录项核对：期望值直接来自规范表格，不从目录反推。
 */
struct CatalogExpectation {
    std::uint16_t oi;                             ///< 附录 E 的对象标识。
    const char* name;                             ///< 附录 E 的中文名称。
    std::uint8_t class_id;                        ///< 接口类号。
    std::uint8_t attribute;                       ///< 待核对属性编号。
    model::DataType type;                         ///< 属性 2 的精确类型。
    std::optional<model::DataType> element_type;  ///< 数组元素的精确类型。
    std::optional<model::ScalerUnit> scaling;     ///< 附录 B 倍率与单位。
    std::string_view unit;                        ///< 附录 B 单位符号。
    bool record = false;                          ///< 是否为记录型属性。
    bool standard_writable = false;               ///< 目录声明的标准写能力。
};

/**
 * @brief 抽查目录项与规范一致性。
 * @note 逐项独立核对 OI、名称、接口类号、类型、元素类型、倍率和单位，
 * 只验证实现自洽的检查无法发现目录整体抄错的问题。
 */
void check_catalog(const std::vector<CatalogExpectation>& expectations) {
    for (const auto& expected : expectations) {
        INFO("OI=" << std::hex << expected.oi << std::dec << " 属性=" << int(expected.attribute));
        const auto* object = standard::find_object(expected.oi);
        REQUIRE(object);
        // OI 与名称直接与附录 E 文本比对。
        CHECK(object->oi == expected.oi);
        CHECK(std::string_view(object->name) == expected.name);
        CHECK(object->class_id == expected.class_id);
        const auto* definition = standard::find_attribute({expected.oi, expected.attribute, 0});
        REQUIRE(definition);
        CHECK(definition->number == expected.attribute);
        CHECK(definition->type == expected.type);
        CHECK(definition->element_type == expected.element_type);
        CHECK(definition->scaling == expected.scaling);
        // 附录 B 的单位符号独立核对，不接受只比对倍率。
        if (expected.scaling) CHECK(standard::unit_symbol(expected.scaling->unit) == expected.unit);
        CHECK(definition->record == expected.record);
        CHECK(definition->writable == expected.standard_writable);
    }
}

/// 三相四费率的三相电能数组，用于标准对象的合法返回值。
model::Array energy_total() {
    return model::Array{{model::UInt32{100}, model::UInt32{10}, model::UInt32{20},
                         model::UInt32{30}, model::UInt32{40}}};
}

}  // namespace

TEST_CASE("标准目录项与规范附录 E 一致", "[service][object][standard]") {
    SECTION("附录 E.1 电能对象：无符号用 uint32，组合有功有符号用 int32") {
        // 表 125：属性 2 是总及费率电能量数组，索引 1 为总量；附录 E.1 规定倍率 0.01kWh。
        check_catalog({
            {oi::forward_active_energy, "正向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
            {oi::reverse_active_energy, "反向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
            // 组合有功电能允许负值，附录 E.1 明确为有符号。
            {oi::combination_active_energy, "组合有功电能", 1, 2, model::DataType::array,
             model::DataType::int32, model::ScalerUnit{-2, 33}, "kWh"},
            {oi::combination_reactive_energy_1, "组合无功1电能", 1, 2, model::DataType::array,
             model::DataType::int32, model::ScalerUnit{-2, 35}, "kvarh"},
            {oi::combination_reactive_energy_2, "组合无功2电能", 1, 2, model::DataType::array,
             model::DataType::int32, model::ScalerUnit{-2, 35}, "kvarh"},
        });
    }

    SECTION("附录 E.1 扩展精度电能使用 64 位整数和更高倍率") {
        // 表 125 属性 4：扩展精度总及费率电能量，倍率 0.0001kWh。
        check_catalog({
            {oi::forward_active_energy, "正向有功电能", 1, 4, model::DataType::array,
             model::DataType::uint64, model::ScalerUnit{-4, 33}, "kWh"},
            {oi::combination_active_energy, "组合有功电能", 1, 4, model::DataType::array,
             model::DataType::int64, model::ScalerUnit{-4, 33}, "kWh"},
        });
    }

    SECTION("附录 E.2 最大需量是结构数组，属性 3 是换算及单位") {
        check_catalog({
            // 表 128：属性 2 为总及费率最大需量，元素是「需量值+发生时间」结构。
            {oi::forward_active_maximum_demand, "正向有功最大需量", 2, 2, model::DataType::array,
             model::DataType::structure, std::nullopt, ""},
            {oi::reverse_active_maximum_demand, "反向有功最大需量", 2, 2, model::DataType::array,
             model::DataType::structure, std::nullopt, ""},
            // 组合无功1最大需量有符号，表 128 允许负值。
            {oi::combination_reactive_1_maximum_demand, "组合无功1最大需量", 2, 2,
             model::DataType::array, model::DataType::structure, std::nullopt, ""},
        });
        const auto* definition =
            standard::find_attribute({oi::forward_active_maximum_demand, 2, 0});
        REQUIRE(definition);
        REQUIRE(definition->element_definition);
        const auto& fields = definition->element_definition->fields;
        REQUIRE(fields.size() == 2);
        // 附录 E.2：需量值倍率为 0.0001kW，发生时间保留 DateTimeS 原字段。
        CHECK(fields[0].type == model::DataType::uint32);
        CHECK(fields[0].scaling == model::ScalerUnit{-4, 28});
        CHECK(fields[1].type == model::DataType::date_time_s);
        CHECK_FALSE(fields[1].scaling.has_value());
        // 属性 3 固定为 scaler_unit，不带倍率。
        const auto* unit = standard::find_attribute({oi::forward_active_maximum_demand, 3, 0});
        REQUIRE(unit);
        CHECK(unit->type == model::DataType::scaler_unit);
        CHECK_FALSE(unit->scaling.has_value());
    }

    SECTION("附录 E.3 电压电流功率频率的量程与单位") {
        check_catalog({
            // 表 130 电压：uint16，倍率 0.1V。
            {oi::voltage, "电压", 3, 2, model::DataType::array, model::DataType::uint16,
             model::ScalerUnit{-1, 38}, "V"},
            // 表 131 电流：int32，倍率 0.001A。
            {oi::current, "电流", 3, 2, model::DataType::array, model::DataType::int32,
             model::ScalerUnit{-3, 36}, "A"},
            // 表 133 有功功率：int32，倍率 0.1W。
            {oi::active_power, "有功功率", 4, 2, model::DataType::array, model::DataType::int32,
             model::ScalerUnit{-1, 27}, "W"},
            // 表 134 无功功率 var、视在功率 VA 共用 0.1 倍率。
            {oi::reactive_power, "无功功率", 4, 2, model::DataType::array, model::DataType::int32,
             model::ScalerUnit{-1, 31}, "var"},
            {oi::apparent_power, "视在功率", 4, 2, model::DataType::array, model::DataType::int32,
             model::ScalerUnit{-1, 29}, "VA"},
            // 表 139 功率因数：int16，倍率 0.001，无单位。
            {oi::power_factor, "功率因数", 4, 2, model::DataType::array, model::DataType::int16,
             model::ScalerUnit{-3, 255}, ""},
            // 电网频率是标量而非数组，倍率 0.01Hz。
            {oi::frequency, "电网频率", 6, 2, model::DataType::uint16, std::nullopt,
             model::ScalerUnit{-2, 47}, "Hz"},
            // 表内温度：int16，倍率 0.1℃。
            {oi::internal_temperature, "表内温度", 6, 2, model::DataType::int16, std::nullopt,
             model::ScalerUnit{-1, 9}, "℃"},
        });
        // 表 131 电流的属性 4 是零线电流，量程与主电流一致。
        const auto* neutral = standard::find_attribute({oi::current, 4, 0});
        REQUIRE(neutral);
        CHECK(neutral->type == model::DataType::int32);
        CHECK(neutral->scaling == model::ScalerUnit{-3, 36});
    }

    SECTION("附录 E.3 谐波与状态字") {
        // 表 137：谐波对象属性 2/3/4 分别是 A/B/C 相，int16，倍率 0.01%。
        check_catalog({
            {oi::voltage_harmonics, "电压谐波含有量", 5, 2, model::DataType::array,
             model::DataType::int16, model::ScalerUnit{-2, 51}, "%"},
            {oi::current_harmonics, "电流谐波含有量", 5, 3, model::DataType::array,
             model::DataType::int16, model::ScalerUnit{-2, 51}, "%"},
        });
        const auto* order = standard::find_attribute({oi::voltage_harmonics, 5, 0});
        REQUIRE(order);
        CHECK(order->type == model::DataType::uint8);
        // 表 140 运行状态字：七个 16 位位串，附录 F.1～F.7 各一组。
        const auto* status = standard::find_attribute({oi::operating_status, 2, 0});
        REQUIRE(status);
        CHECK(status->type == model::DataType::array);
        CHECK(status->element_type == model::DataType::bit_string);
        CHECK(status->layout == standard::ArrayLayout::status_words);
        REQUIRE(status->element_definition);
        CHECK(status->element_definition->size == 16u);
    }

    SECTION("附录 E.5 参数对象的可写能力与取值约束") {
        check_catalog({
            // 表 E.5：表号、客户编号为可写八位串。
            {oi::meter_number, "表号", 8, 2, model::DataType::octet_string, std::nullopt,
             std::nullopt, "", false, true},
            {oi::customer_number, "客户编号", 8, 2, model::DataType::octet_string, std::nullopt,
             std::nullopt, "", false, true},
            // 计量元件数只允许 1、2、3，且标准不提供写能力。
            {oi::metering_element_count, "计量元件数", 8, 2, model::DataType::uint8, std::nullopt,
             std::nullopt, ""},
            // 额定电压等参量是定长可见字符串，只读。
            {oi::rated_voltage, "额定电压", 8, 2, model::DataType::visible_string, std::nullopt,
             std::nullopt, ""},
            {oi::rated_current, "额定电流/基本电流", 8, 2, model::DataType::visible_string,
             std::nullopt, std::nullopt, ""},
            // 最大需量周期与滑差时间是分钟量，倍率 1。
            {oi::maximum_demand_period, "最大需量周期", 8, 2, model::DataType::uint8, std::nullopt,
             model::ScalerUnit{0, 6}, "min", false, true},
            {oi::sliding_interval, "滑差时间", 8, 2, model::DataType::uint8, std::nullopt,
             model::ScalerUnit{0, 6}, "min", false, true},
            // 周休日特征字是 8 位位串。
            {oi::weekend_characteristic, "周休日特征字", 8, 2, model::DataType::bit_string,
             std::nullopt, std::nullopt, "", false, true},
            // 电能表通信地址是可写八位串；附录 E.5 表 146 的对象与属性名均简称为「通信地址」。
            {oi::communication_address, "通信地址", 8, 2, model::DataType::octet_string,
             std::nullopt, std::nullopt, "", false, true},
            // 日期时间属性 2 是 DateTimeS，标准允许校时。
            {oi::date_time, "日期时间", 8, 2, model::DataType::date_time_s, std::nullopt,
             std::nullopt, "", false, true},
        });
        // 计量元件数的取值约束独立核对。
        const auto* elements = standard::find_attribute({oi::metering_element_count, 2, 0});
        REQUIRE(elements);
        REQUIRE(elements->value_definition);
        CHECK(elements->value_definition->allowed_values == std::vector<std::uint64_t>{1, 2, 3});
        // 通信地址属性 2 的名称与 OI 4001 对应。
        const auto* address = standard::find_attribute({oi::communication_address, 2, 0});
        REQUIRE(address);
        CHECK(std::string_view(address->name) == "通信地址");
    }

    SECTION("附录 E.4/E.6 事件与冻结记录属性") {
        check_catalog({
            // 表 143/表 149：记录型属性的 2 是数据表入口。
            {oi::meter_power_down_event, "电能表掉电事件", 7, 2, model::DataType::array,
             model::DataType::structure, std::nullopt, "", true},
            {oi::terminal_initialization_event, "终端初始化事件", 7, 2, model::DataType::array,
             model::DataType::structure, std::nullopt, "", true},
            {oi::daily_freeze, "日冻结", 9, 2, model::DataType::array, model::DataType::structure,
             std::nullopt, "", true},
            {oi::monthly_freeze, "月冻结", 9, 2, model::DataType::array, model::DataType::structure,
             std::nullopt, "", true},
            // 附录 E.3：记录关联列本身是普通数值对象。
            {oi::freeze_time, "数据冻结时间", 8, 2, model::DataType::date_time_s, std::nullopt,
             std::nullopt, ""},
            {oi::event_start_time, "事件发生时间", 8, 2, model::DataType::date_time_s, std::nullopt,
             std::nullopt, ""},
            {oi::event_end_time, "事件结束时间", 8, 2, model::DataType::date_time_s, std::nullopt,
             std::nullopt, ""},
            {oi::freeze_sequence, "冻结记录序号", 8, 2, model::DataType::uint32, std::nullopt,
             std::nullopt, ""},
            {oi::event_sequence, "事件记录序号", 8, 2, model::DataType::uint32, std::nullopt,
             std::nullopt, ""},
        });
    }

    SECTION("相别属于对象标识，同一数值在不同相别上是不同 OI") {
        // 附录 E.1：分相电能是独立对象，OI 低位标识相别。
        check_catalog({
            {oi::forward_active_energy_a, "A相正向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
            {oi::forward_active_energy_b, "B相正向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
            {oi::forward_active_energy_c, "C相正向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
            {oi::reverse_active_energy_a, "A相反向有功电能", 1, 2, model::DataType::array,
             model::DataType::uint32, model::ScalerUnit{-2, 33}, "kWh"},
        });
        REQUIRE(standard::find_object(oi::forward_active_energy_a)->phase == standard::Phase::a);
        REQUIRE(standard::find_object(oi::forward_active_energy_b)->phase == standard::Phase::b);
        REQUIRE(standard::find_object(oi::forward_active_energy_c)->phase == standard::Phase::c);
        // 总电能对象没有相别属性。
        CHECK_FALSE(standard::find_object(oi::forward_active_energy)->phase.has_value());
    }

    SECTION("每个对象的属性 1 恒为逻辑名八位串") {
        for (const auto& object : standard::objects()) {
            INFO("OI=" << object.oi);
            REQUIRE(object.attributes.size() >= 2);
            CHECK(object.attributes[0].number == 1);
            CHECK(object.attributes[0].type == model::DataType::octet_string);
            CHECK(object.attributes[0].name == std::string_view("逻辑名"));
        }
    }
}

TEST_CASE("make_object_schema 只按选择生成只读 schema", "[service][object][standard]") {
    SECTION("schema 复制目录的名称、类型与记录声明") {
        auto schema = make_object_schema(oi::voltage, {1, 2, 3});
        REQUIRE(static_cast<bool>(schema));
        CHECK(schema.value().oi == oi::voltage);
        CHECK(schema.value().name == "电压");
        REQUIRE(schema.value().attributes.size() == 3);
        const auto* logical = attribute_of(schema.value(), 1);
        const auto* value = attribute_of(schema.value(), 2);
        const auto* unit = attribute_of(schema.value(), 3);
        REQUIRE(logical);
        REQUIRE(value);
        REQUIRE(unit);
        CHECK(logical->type == model::DataType::octet_string);
        CHECK(value->type == model::DataType::array);
        CHECK(unit->type == model::DataType::scaler_unit);
        // 元数据可写不代表运行 schema 获得写权限。
        for (const auto& attribute : schema.value().attributes) {
            CHECK(attribute.readable);
            CHECK_FALSE(attribute.writable);
        }
        // 复位/执行等方法没有真实实现，因此不注册任何方法。
        CHECK(schema.value().methods.empty());
    }

    SECTION("记录型属性保留 record 声明") {
        auto schema = make_object_schema(oi::daily_freeze, {2});
        REQUIRE(static_cast<bool>(schema));
        const auto* record = attribute_of(schema.value(), 2);
        REQUIRE(record);
        CHECK(record->record);
        CHECK(record->readable);
        CHECK_FALSE(record->writable);
        CHECK(record->type == model::DataType::array);
    }

    SECTION("标准可写属性在运行 schema 中同样不可写") {
        // 附录 E.5 允许写表号和日期时间，但库要求应用显式授权。
        for (const auto identifier :
             {oi::meter_number, oi::date_time, oi::metering_element_count}) {
            INFO("OI=" << std::hex << identifier << std::dec);
            auto schema = make_object_schema(identifier, {2});
            REQUIRE(static_cast<bool>(schema));
            const auto* attribute = attribute_of(schema.value(), 2);
            REQUIRE(attribute);
            CHECK_FALSE(attribute->writable);
        }
    }

    SECTION("只请求实际选择的属性，其余保持未定义") {
        auto schema = make_object_schema(oi::forward_active_energy, {2});
        REQUIRE(static_cast<bool>(schema));
        CHECK(schema.value().attributes.size() == 1);
        CHECK(standard::find_attribute({oi::forward_active_energy, 3, 0}));
        // 未选择的属性 3 不在 schema 中，经目录读取时是 DAR=4。
        ObjectRegistry registry;
        auto provider = std::make_shared<MemoryObject>();
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(registry.register_object(schema.value(), provider)));
        CHECK(dar_of(registry.read({oi::forward_active_energy, 3, 0})) == 4);
    }

    SECTION("未知 OI 返回 unsupported_tag") {
        auto schema = make_object_schema(0xf123, {2});
        test::require_error(schema, ErrorCode::unsupported_tag);
        CHECK(schema.error().context == "standard object");
    }

    SECTION("已知 OI 的未收录属性返回 unsupported_tag") {
        // 电压对象没有属性 20。
        auto schema = make_object_schema(oi::voltage, {20});
        test::require_error(schema, ErrorCode::unsupported_tag);
        CHECK(schema.error().context == "standard attribute");
        CHECK_FALSE(standard::find_attribute({oi::voltage, 20, 0}));
    }

    SECTION("空选择与超过 31 项的选择被拒绝") {
        auto empty = make_object_schema(oi::voltage, {});
        test::require_error(empty, ErrorCode::invalid_value);
        CHECK(empty.error().context == "standard attribute selection");
        std::vector<std::uint8_t> many(32, 2);
        auto overflow = make_object_schema(oi::voltage, many);
        test::require_error(overflow, ErrorCode::invalid_value);
        CHECK(overflow.error().context == "standard attribute selection");
    }

    SECTION("属性编号零、32 和重复都被拒绝") {
        for (const auto& numbers :
             std::vector<std::vector<std::uint8_t>>{{0}, {32}, {2, 2}, {1, 2, 1}}) {
            INFO("属性选择长度=" << numbers.size());
            auto schema = make_object_schema(oi::voltage, numbers);
            test::require_error(schema, ErrorCode::invalid_value);
            CHECK(schema.error().context == "standard attribute number");
        }
    }

    SECTION("make_object_schema 不修改对象目录") {
        ObjectRegistry registry;
        const auto before = registry.read({oi::voltage, 2, 0});
        REQUIRE(dar_of(before) == 4);
        REQUIRE(static_cast<bool>(make_object_schema(oi::voltage, {2})));
        // 生成 schema 本身不注册任何对象。
        CHECK(dar_of(registry.read({oi::voltage, 2, 0})) == 4);
    }
}

TEST_CASE("register_standard_object 通过只读适配器绑定 provider", "[service][object][standard]") {
    ObjectRegistry registry;
    auto provider = std::make_shared<MemoryObject>();

    SECTION("provider 为空被拒绝") {
        auto result = register_standard_object(registry, oi::voltage, {2}, nullptr);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "standard provider");
    }

    SECTION("非法的设备配置被拒绝且不注册对象") {
        standard::DeviceLayout layout;
        layout.tariff_count = 300;  // 费率数上限为 254。
        auto result = register_standard_object(registry, oi::voltage, {2}, provider, layout);
        test::require_error(result, ErrorCode::invalid_value);
        CHECK(result.error().context == "standard device layout");
        CHECK(dar_of(registry.read({oi::voltage, 2, 0})) == 4);
    }

    SECTION("配置错误与定义错误按各自原因返回") {
        standard::DeviceLayout layout;
        layout.harmonic_order = 1;  // 最低为 2。
        test::require_error(register_standard_object(registry, oi::voltage, {2}, provider, layout),
                            ErrorCode::invalid_value);
        test::require_error(register_standard_object(registry, 0xf123, {2}, provider),
                            ErrorCode::unsupported_tag);
        test::require_error(register_standard_object(registry, oi::voltage, {}, provider),
                            ErrorCode::invalid_value);
        test::require_error(register_standard_object(registry, oi::voltage, {2, 2}, provider),
                            ErrorCode::invalid_value);
    }

    SECTION("重复注册同一 OI 被拒绝且不覆盖已有 provider") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        auto second = std::make_shared<MemoryObject>();
        second->set(2, model::Data{model::Array{{model::UInt32{999}}}});
        auto result = register_standard_object(registry, oi::forward_active_energy, {2}, second);
        test::require_error(result, ErrorCode::busy);
        CHECK(result.error().context == "duplicate OI");
        auto value = registry.read({oi::forward_active_energy, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value) == model::Data{energy_total()});
    }

    SECTION("合法数组按标准倍率与元素类型通过校验") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        auto value = registry.read({oi::forward_active_energy, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value) == model::Data{energy_total()});
        // 索引 1 是总量，适配器按元素类型校验后放行。
        auto total = registry.read({oi::forward_active_energy, 2, 1});
        REQUIRE(std::holds_alternative<model::Data>(total));
        CHECK(std::get<model::Data>(total) == model::Data{model::UInt32{100}});
    }

    SECTION("数组长度与设备配置的费率数不符时返回 DAR=7") {
        // 四费率配置要求五个元素（总量加四费率）。
        provider->set(2, model::Data{model::Array{{model::UInt32{100}, model::UInt32{10}}}});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        CHECK(dar_of(registry.read({oi::forward_active_energy, 2, 0})) == 7);
    }

    SECTION("单费率配置下同样的返回值被接受，验证长度确实由本地配置决定") {
        standard::DeviceLayout layout;
        layout.tariff_count = 1;
        provider->set(2, model::Data{model::Array{{model::UInt32{100}, model::UInt32{10}}}});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider, layout)));
        auto value = registry.read({oi::forward_active_energy, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value).type() == model::DataType::array);
    }

    SECTION("元素类型与标准不符时返回 DAR=7") {
        // 附录 E.1 规定正向有功电能元素是无符号 32 位整数。
        provider->set(
            2, model::Data{model::Array{{model::UInt32{1}, model::UInt16{2}, model::UInt32{3},
                                         model::UInt32{4}, model::UInt32{5}}}});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        CHECK(dar_of(registry.read({oi::forward_active_energy, 2, 0})) == 7);
    }

    SECTION("元素索引越界返回 DAR=8，合法索引放行") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        CHECK(dar_of(registry.read({oi::forward_active_energy, 2, 6})) == 8);
        // 索引 1 是总量，元素类型合法时返回 Data 而不是 DAR。
        auto total = registry.read({oi::forward_active_energy, 2, 1});
        REQUIRE(std::holds_alternative<model::Data>(total));
        CHECK(std::get<model::Data>(total) == model::Data{model::UInt32{100}});
    }

    SECTION("非零特征被适配器拒绝为 DAR=3") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        // 特征位保留在 OAD 中，适配器不伪造快照语义。
        CHECK(dar_of(registry.read({oi::forward_active_energy, 0x22, 0})) == 3);
        // 适配器拒绝时不会调用 provider。
        provider->set(2, model::Data{model::Array{}});
        CHECK(dar_of(registry.read({oi::forward_active_energy, 0x22, 0})) == 3);
    }

    SECTION("provider 的 DAR 原样返回，不由适配器改写") {
        // 未在目录中定义的属性由 MemoryObject 返回 DAR=4，适配器只校验 Data。
        auto missing = std::make_shared<MemoryObject>();
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, missing)));
        CHECK(dar_of(registry.read({oi::forward_active_energy, 2, 0})) == 4);
    }

    SECTION("标准适配器保持只读，写入始终返回 DAR=3") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        CHECK(registry.write(
                  {oi::forward_active_energy, 2, 0},
                  model::Data{model::Array{{model::UInt32{0}, model::UInt32{0}, model::UInt32{0},
                                            model::UInt32{0}, model::UInt32{0}}}}) == 3);
        // 即使目录声明了标准可写属性，适配器也不会授权。
        REQUIRE(
            static_cast<bool>(register_standard_object(registry, oi::meter_number, {2}, provider)));
        CHECK(registry.write({oi::meter_number, 2, 0}, model::Data{model::OctetString{{1, 2}}}) ==
              3);
    }

    SECTION("记录属性只能通过记录查询解释") {
        auto memory = std::make_shared<MemoryObject>();
        REQUIRE(
            static_cast<bool>(register_standard_object(registry, oi::daily_freeze, {2}, memory)));
        // 记录属性按普通属性读取是拒绝。
        CHECK(dar_of(registry.read({oi::daily_freeze, 2, 0})) == 3);
        // 未绑定记录时缺省 DAR=4。
        CHECK(record_dar_of(
                  registry.read_record({{oi::daily_freeze, 2, 0}, model::SelectAll{}, {}})) == 4);
        // 绑定后返回完整表头与行数据。
        const model::Rcsd columns{model::Oad{oi::freeze_time, 2, 0}};
        memory->bind_record(2, [columns](const apdu::GetRecord& query) {
            return RecordResult{query.attribute, columns,
                                std::vector<apdu::RecordRow>{
                                    {model::Data{model::DateTimeS{{0x07, 0xe0, 1, 1, 0, 0, 0}}}}}};
        });
        auto result = registry.read_record({{oi::daily_freeze, 2, 0}, model::SelectAll{}, {}});
        REQUIRE(std::holds_alternative<std::vector<apdu::RecordRow>>(result.result));
        CHECK(result.columns == columns);
        CHECK(std::get<std::vector<apdu::RecordRow>>(result.result).size() == 1);
    }

    SECTION("记录查询的列与行必须符合标准，否则返回 DAR=7") {
        auto memory = std::make_shared<MemoryObject>();
        REQUIRE(
            static_cast<bool>(register_standard_object(registry, oi::daily_freeze, {2}, memory)));
        const apdu::GetRecord query{{oi::daily_freeze, 2, 0}, model::SelectAll{}, {}};
        // 冻结时间列类型正确，适配器放行完整快照。
        memory->bind_record(2, [](const apdu::GetRecord& q) {
            return RecordResult{q.attribute,
                                {model::Oad{oi::freeze_time, 2, 0}},
                                std::vector<apdu::RecordRow>{
                                    {model::Data{model::DateTimeS{{0x07, 0xe0, 1, 1, 0, 0, 0}}}}}};
        });
        auto valid = registry.read_record(query);
        REQUIRE(std::holds_alternative<std::vector<apdu::RecordRow>>(valid.result));
        CHECK(valid.columns == model::Rcsd({model::Oad{oi::freeze_time, 2, 0}}));
        // 列类型错误：冻结时间必须是 DateTimeS。
        memory->bind_record(2, [](const apdu::GetRecord& q) {
            return RecordResult{q.attribute,
                                {model::Oad{oi::freeze_time, 2, 0}},
                                std::vector<apdu::RecordRow>{{model::Data{model::UInt32{1}}}}};
        });
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        // 未定义的列同样被拒绝。
        memory->bind_record(2, [](const apdu::GetRecord& q) {
            return RecordResult{q.attribute,
                                {model::Oad{0xf001, 2, 0}},
                                std::vector<apdu::RecordRow>{{model::Data{model::Null{}}}}};
        });
        CHECK(record_dar_of(registry.read_record(query)) == 7);
        // 行宽与表头不一致由目录先行拒绝。
        memory->bind_record(2, [](const apdu::GetRecord& q) {
            return RecordResult{q.attribute,
                                {model::Oad{oi::freeze_time, 2, 0}},
                                std::vector<apdu::RecordRow>{
                                    {model::Data{model::Null{}}, model::Data{model::Null{}}}}};
        });
        CHECK(record_dar_of(registry.read_record(query)) == 7);
    }

    SECTION("非记录对象与非法查询描述符被拒绝") {
        auto memory = std::make_shared<MemoryObject>();
        REQUIRE(
            static_cast<bool>(register_standard_object(registry, oi::daily_freeze, {2}, memory)));
        // 非记录模板对象没有记录定义。
        CHECK(record_dar_of(registry.read_record({{oi::voltage, 2, 0}, model::SelectAll{}, {}})) ==
              4);
        // 记录查询的属性必须是 2 且索引为零。
        CHECK(record_dar_of(
                  registry.read_record({{oi::daily_freeze, 1, 0}, model::SelectAll{}, {}})) == 4);
        CHECK(record_dar_of(
                  registry.read_record({{oi::daily_freeze, 2, 1}, model::SelectAll{}, {}})) == 8);
        // 重复列在查询阶段就被拒绝，映射为 DAR=8。
        CHECK(record_dar_of(registry.read_record(
                  {{oi::daily_freeze, 2, 0},
                   model::SelectAll{},
                   {model::Oad{oi::freeze_time, 2, 0}, model::Oad{oi::freeze_time, 2, 0}}})) == 8);
        // ROAD 列属于不支持的服务，映射为 DAR=3。
        CHECK(record_dar_of(registry.read_record({{oi::daily_freeze, 2, 0},
                                                  model::SelectAll{},
                                                  {model::Road{{oi::freeze_time, 2, 0}, {}}}})) ==
              3);
    }

    SECTION("资源上限在适配器层生效") {
        provider->set(2, model::Data{energy_total()});
        REQUIRE(static_cast<bool>(
            register_standard_object(registry, oi::forward_active_energy, {2}, provider)));
        // 同样的返回值在默认上限下合法。
        auto value = registry.read({oi::forward_active_energy, 2, 0});
        REQUIRE(std::holds_alternative<model::Data>(value));
        CHECK(std::get<model::Data>(value) == model::Data{energy_total()});
        // 收紧元素上限后，六个节点（根加五元素）的数组被适配器拒绝。
        Limits tight;
        tight.max_elements = 3;
        auto limited = std::make_shared<MemoryObject>();
        limited->set(2, model::Data{energy_total()});
        ObjectRegistry bounded;
        REQUIRE(static_cast<bool>(
            register_standard_object(bounded, oi::forward_active_energy, {2}, limited, {}, tight)));
        CHECK(dar_of(bounded.read({oi::forward_active_energy, 2, 0})) == 7);
    }
}
