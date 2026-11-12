#include "bindings.hpp"

namespace dlt698::python {
namespace m = model;

/** @brief 注册标量 Data 工厂、精确访问器和底层值对象。
 * @tparam T 协议标量包装类型。
 * @param[in,out] module 原生模块。
 * @param[in,out] data Data 绑定。
 * @param[in] name 工厂名称。
 * @param[in] class_name 底层消息值类型名称。
 */
template <class T>
void scalar(py::module_& module, py::class_<m::Data>& data, const char* name,
            const char* class_name) {
    Struct<T> value(module, class_name);
    value.field("value", &T::value).finish();
    data.def_static(
        name,
        [](py::handle input) {
            using V = decltype(T{}.value);
            if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>)
                return m::Data(T{integer<V>(input)});
            else if constexpr (std::is_same_v<V, bool>) {
                if (!PyBool_Check(input.ptr())) throw py::type_error("expected bool");
                return m::Data(T{py::cast<bool>(input)});
            } else if constexpr (std::is_same_v<V, Bytes>)
                return m::Data(T{bytes_from(input)});
            else
                return m::Data(T{py::cast<V>(input)});
        },
        py::arg("value"));
    auto accessor = std::string("as_") + name;
    data.def(accessor.c_str(), [](const m::Data& object) {
        if (!std::holds_alternative<T>(object.payload)) throw py::type_error("Data tag differs");
        const auto& result = object.as<T>().value;
        if constexpr (std::is_same_v<std::decay_t<decltype(result)>, Bytes>)
            return bytes_to(result);
        else
            return result;
    });
}

void bind_model(py::module_& module) {
    auto type = py::enum_<m::DataType>(module, "DataType");
#define DATA_TYPE(name) type.value(#name, m::DataType::name)
    DATA_TYPE(null);
    DATA_TYPE(array);
    DATA_TYPE(structure);
    DATA_TYPE(boolean);
    DATA_TYPE(bit_string);
    DATA_TYPE(int32);
    DATA_TYPE(uint32);
    DATA_TYPE(octet_string);
    DATA_TYPE(visible_string);
    DATA_TYPE(utf8_string);
    DATA_TYPE(int8);
    DATA_TYPE(int16);
    DATA_TYPE(uint8);
    DATA_TYPE(uint16);
    DATA_TYPE(int64);
    DATA_TYPE(uint64);
    DATA_TYPE(enumeration);
    DATA_TYPE(float32);
    DATA_TYPE(float64);
    DATA_TYPE(date_time);
    DATA_TYPE(date);
    DATA_TYPE(time);
    DATA_TYPE(date_time_s);
    DATA_TYPE(oi);
    DATA_TYPE(oad);
    DATA_TYPE(road);
    DATA_TYPE(omd);
    DATA_TYPE(ti);
    DATA_TYPE(tsa);
    DATA_TYPE(mac);
    DATA_TYPE(rn);
    DATA_TYPE(region);
    DATA_TYPE(scaler_unit);
    DATA_TYPE(rsd);
    DATA_TYPE(csd);
    DATA_TYPE(ms);
    DATA_TYPE(sid);
    DATA_TYPE(sid_mac);
    DATA_TYPE(comdcb);
    DATA_TYPE(rcsd);
#undef DATA_TYPE
    Struct<m::Oad> oad(module, "Oad");
    oad.field("oi", &m::Oad::oi)
        .field("attribute", &m::Oad::attribute)
        .field("index", &m::Oad::index)
        .finish();
    oad.cls.def("__eq__", [](const m::Oad& a, const m::Oad& b) { return a == b; });
    Struct<m::Omd> omd(module, "Omd");
    omd.field("oi", &m::Omd::oi)
        .field("method", &m::Omd::method)
        .field("mode", &m::Omd::mode)
        .finish();
    omd.cls.def("__eq__", [](const m::Omd& a, const m::Omd& b) { return a == b; });
    Struct<m::Ti> ti(module, "Ti");
    ti.field("unit", &m::Ti::unit).field("interval", &m::Ti::interval).finish();
    Struct<m::ScalerUnit> scaling(module, "ScalerUnit");
    scaling.field("scaler", &m::ScalerUnit::scaler).field("unit", &m::ScalerUnit::unit).finish();
    Struct<m::BitString> bits(module, "BitString");
    bits.field("bit_count", &m::BitString::bit_count).field("value", &m::BitString::value).finish();
    Struct<m::Sid> sid(module, "Sid");
    sid.field("identifier", &m::Sid::identifier).field("additional", &m::Sid::additional).finish();
    Struct<m::Comdcb> comdcb(module, "Comdcb");
    comdcb.field("baud", &m::Comdcb::baud)
        .field("parity", &m::Comdcb::parity)
        .field("data_bits", &m::Comdcb::data_bits)
        .field("stop_bits", &m::Comdcb::stop_bits)
        .field("flow_control", &m::Comdcb::flow_control)
        .finish();

    auto data = py::class_<m::Data>(module, "Data");
    data.def(py::init<>())
        .def_static("null", [] { return m::Data{}; })
        .def_property_readonly("type", &m::Data::type)
        .def("__eq__", [](const m::Data& a, const m::Data& b) { return a == b; })
        .def("__repr__", [](const m::Data& value) {
            return "Data(tag=" + std::to_string(static_cast<int>(value.type())) + ")";
        });
    scalar<m::Boolean>(module, data, "boolean", "Boolean");
    scalar<m::Int8>(module, data, "int8", "Int8");
    scalar<m::Int16>(module, data, "int16", "Int16");
    scalar<m::Int32>(module, data, "int32", "Int32");
    scalar<m::Int64>(module, data, "int64", "Int64");
    scalar<m::UInt8>(module, data, "uint8", "UInt8");
    scalar<m::UInt16>(module, data, "uint16", "UInt16");
    scalar<m::UInt32>(module, data, "uint32", "UInt32");
    scalar<m::UInt64>(module, data, "uint64", "UInt64");
    scalar<m::Enum>(module, data, "enumeration", "Enumeration");
    scalar<m::Float32>(module, data, "float32", "Float32");
    scalar<m::Float64>(module, data, "float64", "Float64");
    scalar<m::OctetString>(module, data, "octet_string", "OctetString");
    scalar<m::VisibleString>(module, data, "visible_string", "VisibleString");
    scalar<m::Utf8String>(module, data, "utf8_string", "Utf8String");
    scalar<m::DateTime>(module, data, "date_time", "DateTime");
    scalar<m::Date>(module, data, "date", "Date");
    scalar<m::Time>(module, data, "time", "Time");
    scalar<m::DateTimeS>(module, data, "date_time_s", "DateTimeS");
    scalar<m::Oi>(module, data, "oi", "Oi");
    scalar<m::Tsa>(module, data, "tsa", "Tsa");
    scalar<m::Mac>(module, data, "mac", "Mac");
    scalar<m::Rn>(module, data, "rn", "Rn");
    Struct<m::SidMac> sid_mac(module, "SidMac");
    sid_mac.field("sid", &m::SidMac::sid).field("mac", &m::SidMac::mac).finish();
    data.def_static(
            "array",
            [](std::vector<m::Data> values) { return m::Data(m::Array{std::move(values)}); })
        .def_static(
            "structure",
            [](std::vector<m::Data> values) { return m::Data(m::Structure{std::move(values)}); })
        .def_static("oad", [](m::Oad value) { return m::Data(value); })
        .def_static("omd", [](m::Omd value) { return m::Data(value); })
        .def_static("ti", [](m::Ti value) { return m::Data(value); })
        .def_static("scaler_unit", [](m::ScalerUnit value) { return m::Data(value); })
        .def_static("sid", [](m::Sid value) { return m::Data(value); })
        .def_static("sid_mac", [](m::SidMac value) { return m::Data(value); })
        .def_static("comdcb", [](m::Comdcb value) { return m::Data(value); })
        .def_static(
            "bit_string",
            [](std::size_t count, py::handle bytes) {
                return m::Data(m::BitString{count, bytes_from(bytes)});
            },
            py::arg("bit_count"), py::arg("value"))
        .def_property_readonly("value", [](const m::Data& value) -> py::object {
            // 字段与嵌套容器返回副本，不让 Python 修改正在被会话使用的快照。
            return std::visit(
                [](const auto& v) -> py::object {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, m::Null>)
                        return py::none();
                    else if constexpr (std::is_same_v<T, m::RecordData>) {
                        switch (v.type()) {
                            case m::DataType::road:
                                return py::cast(v.template as<m::Road>());
                            case m::DataType::region:
                                return py::cast(v.template as<m::Region>());
                            case m::DataType::rsd:
                                return py::cast(v.template as<m::Rsd>());
                            case m::DataType::csd:
                                return py::cast(v.template as<m::Csd>());
                            case m::DataType::ms:
                                return py::cast(v.template as<m::Ms>());
                            case m::DataType::rcsd:
                                return py::cast(v.template as<m::Rcsd>());
                            default:
                                throw py::value_error("moved record value");
                        }
                    } else if constexpr (std::is_same_v<T, m::Oad> || std::is_same_v<T, m::Omd> ||
                                         std::is_same_v<T, m::Ti> ||
                                         std::is_same_v<T, m::ScalerUnit> ||
                                         std::is_same_v<T, m::Sid> ||
                                         std::is_same_v<T, m::SidMac> ||
                                         std::is_same_v<T, m::Comdcb> ||
                                         std::is_same_v<T, m::BitString>)
                        return py::cast(v);
                    else if constexpr (std::is_same_v<std::decay_t<decltype(v.value)>, Bytes>)
                        return bytes_to(v.value);
                    else
                        return py::cast(v.value);
                },
                value.payload);
        });
}
}  // namespace dlt698::python
