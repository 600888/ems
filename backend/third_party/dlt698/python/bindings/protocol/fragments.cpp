#include <dlt698/protocol/apdu/get_block.hpp>
#include <dlt698/protocol/link/fragment.hpp>

#include "bindings.hpp"

namespace dlt698::python {
void bind_fragments(py::module_& module) {
    namespace l = protocol::link;
    namespace a = protocol::apdu;
    py::enum_<l::FragmentType>(module, "FragmentType")
        .value("first", l::FragmentType::first)
        .value("last", l::FragmentType::last)
        .value("acknowledgement", l::FragmentType::acknowledgement)
        .value("middle", l::FragmentType::middle);
    Struct<l::Fragment>(module, "Fragment")
        .field("type", &l::Fragment::type)
        .field("sequence", &l::Fragment::sequence)
        .field("data", &l::Fragment::data)
        .finish();
    Struct<l::Reassembly>(module, "Reassembly")
        .field("acknowledge", &l::Reassembly::acknowledge)
        .field("apdu", &l::Reassembly::apdu)
        .field("duplicate", &l::Reassembly::duplicate)
        .finish();
    module.def(
        "encode_fragment",
        [](const l::Fragment& fragment) { return unwrap(l::encode_fragment(fragment)); },
        py::arg("fragment"));
    module.def(
        "decode_fragment",
        [](py::handle payload) {
            auto bytes = bytes_from(payload);
            return unwrap(l::decode_fragment(bytes));
        },
        py::arg("payload"));
    py::class_<l::LinkFragmenter>(module, "LinkFragmenter")
        .def(py::init<Bytes, std::size_t, std::size_t>(), py::arg("apdu"),
             py::arg("fragment_bytes"), py::arg("limit"))
        .def("current", &l::LinkFragmenter::current)
        .def(
            "acknowledge",
            [](l::LinkFragmenter& value, std::uint16_t sequence) {
                unwrap(value.acknowledge(sequence));
            },
            py::arg("sequence"));
    py::class_<l::LinkReassembler>(module, "LinkReassembler")
        .def(py::init<std::size_t>(), py::arg("limit"))
        .def(
            "accept",
            [](l::LinkReassembler& value, const l::Fragment& fragment) {
                return unwrap(value.accept(fragment));
            },
            py::arg("fragment"))
        .def("reset", &l::LinkReassembler::reset)
        .def_property_readonly("active", &l::LinkReassembler::active);
    py::class_<a::GetBlockTransfer>(module, "GetBlockTransfer")
        .def(py::init<std::uint8_t, bool, Limits, bool>(), py::arg("piid"), py::arg("records"),
             py::arg("limits") = Limits{}, py::arg("merge_record_rows") = true)
        .def_static(
            "split",
            [](a::GetSnapshot snapshot, std::size_t target_bytes, const Limits& limits) {
                return unwrap(
                    a::GetBlockTransfer::split(std::move(snapshot), target_bytes, limits));
            },
            py::arg("snapshot"), py::arg("target_bytes"), py::arg("limits") = Limits{})
        .def(
            "accept",
            [](a::GetBlockTransfer& value, const a::GetNextResponse& block) {
                return unwrap(value.accept(block));
            },
            py::arg("block"));
}
}  // namespace dlt698::python
