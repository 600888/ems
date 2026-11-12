#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/standard/records.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_records(py::module_& module) {
    Struct<model::Road> value_Road(module, "Road");
    Struct<model::Region> value_Region(module, "Region");
    Struct<model::NoMeters> value_NoMeters(module, "NoMeters");
    Struct<model::AllMeters> value_AllMeters(module, "AllMeters");
    Struct<model::MeterTypes> value_MeterTypes(module, "MeterTypes");
    Struct<model::MeterAddresses> value_MeterAddresses(module, "MeterAddresses");
    Struct<model::MeterNumbers> value_MeterNumbers(module, "MeterNumbers");
    Struct<model::MeterTypeRegions> value_MeterTypeRegions(module, "MeterTypeRegions");
    Struct<model::MeterAddressRegions> value_MeterAddressRegions(module, "MeterAddressRegions");
    Struct<model::MeterNumberRegions> value_MeterNumberRegions(module, "MeterNumberRegions");
    Struct<model::SelectAll> value_SelectAll(module, "SelectAll");
    Struct<model::Selector1> value_Selector1(module, "Selector1");
    Struct<model::Selector2> value_Selector2(module, "Selector2");
    Struct<model::Selector3> value_Selector3(module, "Selector3");
    Struct<model::Selector4> value_Selector4(module, "Selector4");
    Struct<model::Selector5> value_Selector5(module, "Selector5");
    Struct<model::Selector6> value_Selector6(module, "Selector6");
    Struct<model::Selector7> value_Selector7(module, "Selector7");
    Struct<model::Selector8> value_Selector8(module, "Selector8");
    Struct<model::Selector9> value_Selector9(module, "Selector9");
    Struct<model::Selector10> value_Selector10(module, "Selector10");
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static("road", [](model::Road value) {
            return model::Data(model::RecordData(std::move(value)));
        });
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static("region", [](model::Region value) {
            return model::Data(model::RecordData(std::move(value)));
        });
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static("rsd", [](model::Rsd value) {
            return model::Data(model::RecordData(std::move(value)));
        });
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static("csd", [](model::Csd value) {
            return model::Data(model::RecordData(std::move(value)));
        });
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static(
            "ms", [](model::Ms value) { return model::Data(model::RecordData(std::move(value))); });
    py::reinterpret_borrow<py::class_<model::Data>>(module.attr("Data"))
        .def_static("rcsd", [](model::Rcsd value) {
            return model::Data(model::RecordData(std::move(value)));
        });
    value_Road.field("attribute", &model::Road::attribute);
    value_Road.field("associated", &model::Road::associated);
    value_Road.finish();
    value_Region.field("boundary", &model::Region::boundary);
    value_Region.field("begin", &model::Region::begin);
    value_Region.field("end", &model::Region::end);
    value_Region.finish();
    value_NoMeters.finish();
    value_AllMeters.finish();
    value_MeterTypes.field("values", &model::MeterTypes::values);
    value_MeterTypes.finish();
    value_MeterAddresses.field("values", &model::MeterAddresses::values);
    value_MeterAddresses.finish();
    value_MeterNumbers.field("values", &model::MeterNumbers::values);
    value_MeterNumbers.finish();
    value_MeterTypeRegions.field("values", &model::MeterTypeRegions::values);
    value_MeterTypeRegions.finish();
    value_MeterAddressRegions.field("values", &model::MeterAddressRegions::values);
    value_MeterAddressRegions.finish();
    value_MeterNumberRegions.field("values", &model::MeterNumberRegions::values);
    value_MeterNumberRegions.finish();
    value_SelectAll.finish();
    value_Selector1.field("attribute", &model::Selector1::attribute);
    value_Selector1.field("value", &model::Selector1::value);
    value_Selector1.finish();
    value_Selector2.field("attribute", &model::Selector2::attribute);
    value_Selector2.field("begin", &model::Selector2::begin);
    value_Selector2.field("end", &model::Selector2::end);
    value_Selector2.field("interval", &model::Selector2::interval);
    value_Selector2.finish();
    value_Selector3.field("ranges", &model::Selector3::ranges);
    value_Selector3.finish();
    value_Selector4.field("time", &model::Selector4::time);
    value_Selector4.field("meters", &model::Selector4::meters);
    value_Selector4.finish();
    value_Selector5.field("time", &model::Selector5::time);
    value_Selector5.field("meters", &model::Selector5::meters);
    value_Selector5.finish();
    value_Selector6.field("begin", &model::Selector6::begin);
    value_Selector6.field("end", &model::Selector6::end);
    value_Selector6.field("interval", &model::Selector6::interval);
    value_Selector6.field("meters", &model::Selector6::meters);
    value_Selector6.finish();
    value_Selector7.field("begin", &model::Selector7::begin);
    value_Selector7.field("end", &model::Selector7::end);
    value_Selector7.field("interval", &model::Selector7::interval);
    value_Selector7.field("meters", &model::Selector7::meters);
    value_Selector7.finish();
    value_Selector8.field("begin", &model::Selector8::begin);
    value_Selector8.field("end", &model::Selector8::end);
    value_Selector8.field("interval", &model::Selector8::interval);
    value_Selector8.field("meters", &model::Selector8::meters);
    value_Selector8.finish();
    value_Selector9.field("previous", &model::Selector9::previous);
    value_Selector9.finish();
    value_Selector10.field("latest", &model::Selector10::latest);
    value_Selector10.field("meters", &model::Selector10::meters);
    value_Selector10.finish();
}
}  // namespace dlt698::python
