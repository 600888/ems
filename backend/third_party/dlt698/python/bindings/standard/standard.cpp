#include <dlt698/standard/capabilities.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
namespace s = standard;

void bind_standard_enums(py::module_& module) {
    py::enum_<s::Wiring>(module, "Wiring")
        .value("single_phase", s::Wiring::single_phase)
        .value("three_phase", s::Wiring::three_phase);
    py::enum_<s::Phase>(module, "Phase")
        .value("total", s::Phase::total)
        .value("a", s::Phase::a)
        .value("b", s::Phase::b)
        .value("c", s::Phase::c);
    auto layout = py::enum_<s::ArrayLayout>(module, "ArrayLayout");
#define LAYOUT(name) layout.value(#name, s::ArrayLayout::name)
    LAYOUT(none);
    LAYOUT(phases);
    LAYOUT(total_phases);
    LAYOUT(total_tariffs);
    LAYOUT(harmonics);
    LAYOUT(status_words);
    LAYOUT(variable);
#undef LAYOUT
}

void bind_standard(py::module_& module) {
    py::enum_<s::Support>(module, "Support")
        .value("unknown", s::Support::unknown)
        .value("no", s::Support::no)
        .value("yes", s::Support::yes);
    py::enum_<s::ReadService>(module, "ReadService")
        .value("normal", s::ReadService::normal)
        .value("list", s::ReadService::list)
        .value("record", s::ReadService::record)
        .value("next", s::ReadService::next);
    Struct<s::Capabilities>(module, "Capabilities")
        .field("negotiated", &s::Capabilities::negotiated)
        .finish();
    Struct<s::ReadBatch>(module, "ReadBatch")
        .field("list", &s::ReadBatch::list)
        .field("attributes", &s::ReadBatch::attributes)
        .finish();
    Struct<s::CandidatePoint>(module, "CandidatePoint")
        .field("attribute", &s::CandidatePoint::attribute)
        .field("hint", &s::CandidatePoint::hint)
        .finish();
    module.def(
        "capabilities_from_connect",
        [](const protocol::apdu::ConnectResponse& response) {
            return unwrap(s::capabilities_from_connect(response));
        },
        py::arg("response"));
    module.def("service_support", &s::service_support, py::arg("capabilities"), py::arg("service"));
    module.def("point_support", &s::point_support, py::arg("capabilities"), py::arg("attribute"));
    module.def("candidate_points", &s::candidate_points, py::arg("capabilities"),
               py::arg("attributes"), py::arg("discard_negative") = false);
    module.def(
        "plan_reads",
        [](const s::Capabilities& capabilities, const std::vector<model::Oad>& attributes,
           std::size_t batch_size, const Limits& limits) {
            return unwrap(s::plan_reads(capabilities, attributes, batch_size, limits));
        },
        py::arg("capabilities"), py::arg("attributes"), py::arg("batch_size") = 16,
        py::arg("limits") = Limits{});
    module.def(
        "require_record_service",
        [](const s::Capabilities& capabilities) {
            unwrap(s::require_record_service(capabilities));
        },
        py::arg("capabilities"));
    module.def(
        "validate_record_time",
        [](const model::DateTimeS& time) { unwrap(s::validate_record_time(time)); },
        py::arg("time"));
    module.def(
        "validate_record_query",
        [](const protocol::apdu::GetRecord& query, const s::DeviceLayout& layout,
           const Limits& limits) { unwrap(s::validate_record_query(query, layout, limits)); },
        py::arg("query"), py::arg("layout") = s::DeviceLayout{}, py::arg("limits") = Limits{});
    module.def(
        "validate_record_cell",
        [](std::uint16_t oi, const model::Oad& column, const model::Data& value,
           const s::DeviceLayout& layout, const Limits& limits) {
            unwrap(s::validate_record_cell(oi, column, value, layout, limits));
        },
        py::arg("oi"), py::arg("column"), py::arg("value"), py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    module.def(
        "validate_record_result",
        [](const protocol::apdu::GetRecord& query, const protocol::apdu::RecordResult& result,
           const s::DeviceLayout& layout, const Limits& limits) {
            unwrap(s::validate_record_result(query, result, layout, limits));
        },
        py::arg("query"), py::arg("result"), py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    py::class_<s::ValueDefinition>(module, "ValueDefinition")
        .def_readonly("name", &s::ValueDefinition::name)
        .def_readonly("type", &s::ValueDefinition::type)
        .def_readonly("scaling", &s::ValueDefinition::scaling)
        .def_readonly("size", &s::ValueDefinition::size)
        .def_readonly("fields", &s::ValueDefinition::fields)
        .def_readonly("maximum", &s::ValueDefinition::maximum)
        .def_readonly("allowed_values", &s::ValueDefinition::allowed_values);
    py::class_<s::AttributeDefinition>(module, "AttributeDefinition")
        .def_readonly("number", &s::AttributeDefinition::number)
        .def_readonly("name", &s::AttributeDefinition::name)
        .def_readonly("type", &s::AttributeDefinition::type)
        .def_readonly("element_type", &s::AttributeDefinition::element_type)
        .def_readonly("layout", &s::AttributeDefinition::layout)
        .def_readonly("scaling", &s::AttributeDefinition::scaling)
        .def_readonly("readable", &s::AttributeDefinition::readable)
        .def_readonly("writable", &s::AttributeDefinition::writable)
        .def_readonly("record", &s::AttributeDefinition::record)
        .def_readonly("source", &s::AttributeDefinition::source)
        .def_readonly("element_definition", &s::AttributeDefinition::element_definition)
        .def_readonly("value_definition", &s::AttributeDefinition::value_definition);
    py::class_<s::ObjectDefinition>(module, "ObjectDefinition")
        .def_readonly("oi", &s::ObjectDefinition::oi)
        .def_readonly("name", &s::ObjectDefinition::name)
        .def_readonly("class_id", &s::ObjectDefinition::class_id)
        .def_readonly("version", &s::ObjectDefinition::version)
        .def_readonly("source", &s::ObjectDefinition::source)
        .def_readonly("attributes", &s::ObjectDefinition::attributes)
        .def_readonly("phase", &s::ObjectDefinition::phase);
    Struct<s::ScaledNumber> number(module, "ScaledNumber");
    number.field("raw", &s::ScaledNumber::raw).field("scaling", &s::ScaledNumber::scaling).finish();
    Struct<s::DemandValue> demand(module, "DemandValue");
    demand.field("number", &s::DemandValue::number)
        .field("occurred_at", &s::DemandValue::occurred_at)
        .finish();
    py::class_<s::RecordDefinition>(module, "RecordDefinition")
        .def_readonly("oi", &s::RecordDefinition::oi)
        .def_readonly("name", &s::RecordDefinition::name)
        .def_readonly("sequence", &s::RecordDefinition::sequence)
        .def_readonly("time", &s::RecordDefinition::time)
        .def_readonly("base_columns", &s::RecordDefinition::base_columns);
    module.def("objects", [] { return s::objects(); });
    module.def("find_object", &s::find_object, py::return_value_policy::copy);
    module.def("find_attribute", &s::find_attribute, py::return_value_policy::copy);
    module.def("find_record", &s::find_record, py::return_value_policy::copy);
    module.def("validate_layout",
               [](const s::DeviceLayout& value) { unwrap(s::validate_layout(value)); });
    module.def(
        "make_oad",
        [](std::uint16_t oi, std::size_t attr, std::size_t index, const s::DeviceLayout& layout) {
            return unwrap(s::make_oad(oi, attr, index, layout));
        },
        py::arg("oi"), py::arg("attribute") = 2, py::arg("index") = 0,
        py::arg("layout") = s::DeviceLayout{});
    module.def(
        "phase_oad",
        [](std::uint16_t oi, s::Phase phase, const s::DeviceLayout& layout) {
            return unwrap(s::phase_oad(oi, phase, layout));
        },
        py::arg("oi"), py::arg("phase"), py::arg("layout") = s::DeviceLayout{});
    module.def(
        "tariff_oad",
        [](std::uint16_t oi, std::size_t tariff, const s::DeviceLayout& layout, bool precision) {
            return unwrap(s::tariff_oad(oi, tariff, layout, precision));
        },
        py::arg("oi"), py::arg("tariff"), py::arg("layout") = s::DeviceLayout{},
        py::arg("high_precision") = false);
    module.def(
        "harmonic_oad",
        [](std::uint16_t oi, s::Phase phase, std::size_t order, const s::DeviceLayout& layout) {
            return unwrap(s::harmonic_oad(oi, phase, order, layout));
        },
        py::arg("oi"), py::arg("phase"), py::arg("order"), py::arg("layout") = s::DeviceLayout{});
    module.def(
        "validate_value",
        [](const model::Oad& attr, const model::Data& value, const s::DeviceLayout& layout,
           const Limits& limits) { unwrap(s::validate_value(attr, value, layout, limits)); },
        py::arg("attribute"), py::arg("value"), py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    module.def(
        "engineering_values",
        [](const model::Oad& attr, const model::Data& value, const s::DeviceLayout& layout,
           const Limits& limits) {
            return unwrap(s::engineering_values(attr, value, layout, limits));
        },
        py::arg("attribute"), py::arg("value"), py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    module.def("decimal_text", &s::decimal_text);
    module.def(
        "validate_oad",
        [](const model::Oad& attr, const s::DeviceLayout& layout) {
            unwrap(s::validate_oad(attr, layout));
        },
        py::arg("attribute"), py::arg("layout") = s::DeviceLayout{});
    module.def(
        "energy_oad",
        [](std::uint16_t family, s::Phase phase, std::size_t tariff, const s::DeviceLayout& layout,
           bool precision) {
            return unwrap(s::energy_oad(family, phase, tariff, layout, precision));
        },
        py::arg("family"), py::arg("phase"), py::arg("tariff"),
        py::arg("layout") = s::DeviceLayout{}, py::arg("high_precision") = false);
    module.def(
        "demand_oad",
        [](std::uint16_t family, s::Phase phase, std::size_t tariff,
           const s::DeviceLayout& layout) {
            return unwrap(s::demand_oad(family, phase, tariff, layout));
        },
        py::arg("family"), py::arg("phase"), py::arg("tariff"),
        py::arg("layout") = s::DeviceLayout{});
    module.def(
        "demand_values",
        [](const model::Oad& attr, const model::Data& value, const s::DeviceLayout& layout,
           const Limits& limits) { return unwrap(s::demand_values(attr, value, layout, limits)); },
        py::arg("attribute"), py::arg("value"), py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    module.def("approximate_value", &s::approximate_value);
    module.def("unit_symbol", [](std::uint8_t unit) { return std::string(s::unit_symbol(unit)); });
    module.def(
        "make_record_query",
        [](std::uint16_t oi, model::Rsd rows, std::vector<model::Oad> columns,
           const s::DeviceLayout& layout, const Limits& limits) {
            return unwrap(
                s::make_record_query(oi, std::move(rows), std::move(columns), layout, limits));
        },
        py::arg("oi"), py::arg("rows"), py::arg("columns") = std::vector<model::Oad>{},
        py::arg("layout") = s::DeviceLayout{}, py::arg("limits") = Limits{});
    module.def(
        "record_at",
        [](std::uint16_t oi, model::DateTimeS time, std::vector<model::Oad> columns,
           const s::DeviceLayout& layout, const Limits& limits) {
            return unwrap(s::record_at(oi, time, std::move(columns), layout, limits));
        },
        py::arg("oi"), py::arg("time"), py::arg("columns") = std::vector<model::Oad>{},
        py::arg("layout") = s::DeviceLayout{}, py::arg("limits") = Limits{});
    module.def(
        "record_between",
        [](std::uint16_t oi, model::DateTimeS begin, model::DateTimeS end,
           std::vector<model::Oad> columns, const s::DeviceLayout& layout, const Limits& limits) {
            return unwrap(s::record_between(oi, begin, end, std::move(columns), layout, limits));
        },
        py::arg("oi"), py::arg("begin"), py::arg("end"),
        py::arg("columns") = std::vector<model::Oad>{}, py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
    module.def(
        "record_sequences",
        [](std::uint16_t oi, std::uint32_t begin, std::uint32_t end,
           std::vector<model::Oad> columns, const s::DeviceLayout& layout, const Limits& limits) {
            return unwrap(s::record_sequences(oi, begin, end, std::move(columns), layout, limits));
        },
        py::arg("oi"), py::arg("begin"), py::arg("end"),
        py::arg("columns") = std::vector<model::Oad>{}, py::arg("layout") = s::DeviceLayout{},
        py::arg("limits") = Limits{});
}
}  // namespace dlt698::python
