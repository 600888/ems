#include <dlt698/protocol/apdu/security.hpp>
#include <dlt698/protocol/link/frame.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_codec(py::module_& module) {
    module.def(
        "decode_data",
        [](py::handle input, const Limits& limits) {
            auto bytes = bytes_from(input);
            return unwrap(codec::decode_data(bytes, limits));
        },
        py::arg("input"), py::arg("limits") = Limits{});
    module.def(
        "encode_data",
        [](const model::Data& value, const Limits& limits) {
            return bytes_to(unwrap(codec::encode_data(value, limits)));
        },
        py::arg("value"), py::arg("limits") = Limits{});
    module.def(
        "decode_frame",
        [](py::handle input, const Limits& limits) {
            auto bytes = bytes_from(input);
            return unwrap(protocol::link::decode_frame(bytes, limits));
        },
        py::arg("input"), py::arg("limits") = Limits{});
    module.def(
        "encode_frame",
        [](const protocol::link::Frame& value, const Limits& limits) {
            return bytes_to(unwrap(protocol::link::encode_frame(value, limits)));
        },
        py::arg("value"), py::arg("limits") = Limits{});
    module.def("crc16", [](py::handle input) {
        auto bytes = bytes_from(input);
        return protocol::link::crc16(bytes);
    });
    module.def(
        "decode_apdu",
        [](py::handle input, const Limits& limits) {
            auto bytes = bytes_from(input);
            return unwrap(protocol::apdu::decode_apdu(bytes, limits));
        },
        py::arg("input"), py::arg("limits") = Limits{});
    module.def(
        "encode_apdu",
        [](const protocol::apdu::Apdu& value, const Limits& limits) {
            return bytes_to(unwrap(protocol::apdu::encode_apdu(value, limits)));
        },
        py::arg("value"), py::arg("limits") = Limits{});
    module.def(
        "decode_security",
        [](py::handle input, const Limits& limits) {
            auto bytes = bytes_from(input);
            return unwrap(protocol::apdu::decode_security(bytes, limits));
        },
        py::arg("input"), py::arg("limits") = Limits{});
    module.def(
        "encode_security",
        [](const protocol::apdu::SecurityApdu& value, const Limits& limits) {
            return bytes_to(unwrap(protocol::apdu::encode_security(value, limits)));
        },
        py::arg("value"), py::arg("limits") = Limits{});
    module.def("advanced_capability", &protocol::apdu::advanced_capability);
    module.def("advanced_matches", &protocol::apdu::advanced_matches);
    py::class_<protocol::link::FrameStreamDecoder>(module, "FrameStreamDecoder")
        .def(py::init<Limits>(), py::arg("limits") = Limits{})
        .def("feed",
             [](protocol::link::FrameStreamDecoder& decoder, py::handle input) {
                 auto bytes = bytes_from(input);
                 return decoder.feed(bytes);
             })
        .def("reset", &protocol::link::FrameStreamDecoder::reset)
        .def_property_readonly("buffered_size", &protocol::link::FrameStreamDecoder::buffered_size);
}
}  // namespace dlt698::python
