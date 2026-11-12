#include "bindings.hpp"
#include "build_info.hpp"

PYBIND11_MODULE(_native, module) {
    module.doc() = "同一 C++17 内核的拥有型 Python 接口；阻塞调用释放 GIL。";
    module.def("build_info", [] {
        pybind11::dict info;
        info["version"] = DLT698_PY_VERSION;
        info["commit"] = DLT698_PY_COMMIT;
        info["source_sha256"] = DLT698_PY_SOURCE_SHA256;
        info["api_sha256"] = DLT698_PY_API_SHA256;
        info["mode"] = DLT698_PY_MODE;
        return info;
    });
    dlt698::python::bind_errors(module);
    dlt698::python::bind_enums(module);
    dlt698::python::bind_standard_enums(module);
    dlt698::python::bind_security_type(module);
    dlt698::python::bind_model(module);
    dlt698::python::bind_records(module);
    dlt698::python::bind_messages(module);
    dlt698::python::bind_connection(module);
    dlt698::python::bind_mutation(module);
    dlt698::python::bind_advanced(module);
    dlt698::python::bind_options(module);
    dlt698::python::bind_standard(module);
    dlt698::python::bind_codec(module);
    dlt698::python::bind_fragments(module);
    dlt698::python::bind_app(module);
    dlt698::python::bind_expert(module);
    dlt698::python::bind_channels(module);
}
