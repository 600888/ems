#include "bindings.hpp"

namespace dlt698::python {
py::str error_text(const std::string& value) {
    auto decoded =
        PyUnicode_DecodeUTF8(value.data(), static_cast<Py_ssize_t>(value.size()), "strict");
    if (decoded) return py::reinterpret_steal<py::str>(decoded);
    PyErr_Clear();
    // 系统错误来自 Asio 的本地代码页；不能把它强行按 UTF-8 转换而掩盖原错误。
#ifdef _WIN32
    return py::bytes(value).attr("decode")("mbcs", "replace").cast<py::str>();
#else
    return py::bytes(value).attr("decode")("utf-8", "replace").cast<py::str>();
#endif
}

[[noreturn]] void raise_error(const Error& error) {
    // 延迟查找 Python 异常类型，不在原生静态存储中持有 Python 引用。
    auto errors = py::module_::import("dlt698.errors");
    auto instance =
        errors.attr("_from_native")(error.code, error.offset, error_text(error.context),
                                    py::cast(error.remote_code), py::bytes(error.context));
    PyErr_SetObject(reinterpret_cast<PyObject*>(Py_TYPE(instance.ptr())), instance.ptr());
    throw py::error_already_set();
}

void bind_errors(py::module_& module) {
    auto code = py::enum_<ErrorCode>(module, "ErrorCode");
#define ERROR_CODE(name) code.value(#name, ErrorCode::name)
    ERROR_CODE(need_more_data);
    ERROR_CODE(invalid_length);
    ERROR_CODE(invalid_value);
    ERROR_CODE(unsupported_tag);
    ERROR_CODE(unsupported_service);
    ERROR_CODE(checksum_header);
    ERROR_CODE(checksum_frame);
    ERROR_CODE(resource_limit);
    ERROR_CODE(trailing_data);
    ERROR_CODE(closed);
    ERROR_CODE(io_error);
    ERROR_CODE(timeout);
    ERROR_CODE(cancelled);
    ERROR_CODE(busy);
    ERROR_CODE(address_mismatch);
    ERROR_CODE(direction_mismatch);
    ERROR_CODE(not_associated);
    ERROR_CODE(association_failed);
    ERROR_CODE(remote_error);
#undef ERROR_CODE
    Struct<Limits> limits(module, "Limits");
    limits.field("max_data_bytes", &Limits::max_data_bytes)
        .field("max_elements", &Limits::max_elements)
        .field("max_depth", &Limits::max_depth)
        .field("max_frame_bytes", &Limits::max_frame_bytes)
        .field("max_stream_bytes", &Limits::max_stream_bytes)
        .finish();
    Struct<Error> error(module, "Error");
    error.field("code", &Error::code)
        .field("offset", &Error::offset)
        .field("context", &Error::context)
        .field("remote_code", &Error::remote_code)
        .finish();
    error.cls.def_property(
        "context", [](const Error& value) { return error_text(value.context); },
        [](Error& value, std::string context) { value.context = std::move(context); });
    error.cls.def_property_readonly("context_bytes",
                                    [](const Error& value) { return py::bytes(value.context); });
}
}  // namespace dlt698::python
