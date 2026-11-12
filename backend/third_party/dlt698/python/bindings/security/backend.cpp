#include <pybind11/trampoline_self_life_support.h>

#include <dlt698/security/backend.hpp>

#include "bindings.hpp"

namespace dlt698::python {
class PythonBackend : public security::IBackend, public py::trampoline_self_life_support {
   public:
    // 返回值成功解包；Python 分类异常恢复为原生错误，其余异常保存文本并隔离。
    template <class T, class... Args>
    Result<T> call(const char* name, Args&&... args) {
        py::gil_scoped_acquire acquire;
        auto method = py::get_override(this, name);
        if (!method) return Error{ErrorCode::unsupported_service, 0, name};
        try {
            if constexpr (std::is_void_v<T>) {
                if (!method(std::forward<Args>(args)...).is_none())
                    return Error{ErrorCode::invalid_value, 0, "security callback must return None"};
                return {};
            } else
                return py::cast<T>(method(std::forward<Args>(args)...));
        } catch (py::error_already_set& error) {
            if (py::hasattr(error.value(), "code") && py::hasattr(error.value(), "context")) {
                auto value = error.value();
                return Error{py::cast<ErrorCode>(value.attr("code")),
                             py::cast<std::size_t>(value.attr("offset")),
                             py::cast<std::string>(value.attr("context")),
                             py::cast<std::optional<std::uint8_t>>(value.attr("remote_code"))};
            }
            return Error{ErrorCode::invalid_value, 0, error.what()};
        } catch (const std::exception& error) {
            return Error{ErrorCode::invalid_value, 0, error.what()};
        }
    }

    Result<protocol::apdu::ConnectMechanism> begin_connect() override {
        return call<protocol::apdu::ConnectMechanism>("begin_connect");
    }

    Result<security::AuthenticationResult> accept_connect(
        const protocol::apdu::ConnectRequest& request) override {
        return call<security::AuthenticationResult>("accept_connect",
                                                    protocol::apdu::ConnectRequest(request));
    }

    Result<void> verify_connect(const protocol::apdu::ConnectRequest& request,
                                const protocol::apdu::ConnectResponse& response) override {
        return call<void>("verify_connect", protocol::apdu::ConnectRequest(request),
                          protocol::apdu::ConnectResponse(response));
    }

    Result<protocol::apdu::SecurityRequest> protect_request(ByteView application) override {
        py::gil_scoped_acquire acquire;
        return call<protocol::apdu::SecurityRequest>(
            "protect_request",
            py::bytes(reinterpret_cast<const char*>(application.data()), application.size()));
    }

    Result<Bytes> open_request(const protocol::apdu::SecurityRequest& message) override {
        return call<Bytes>("open_request", protocol::apdu::SecurityRequest(message));
    }

    Result<protocol::apdu::SecurityResponse> protect_response(ByteView application) override {
        py::gil_scoped_acquire acquire;
        return call<protocol::apdu::SecurityResponse>(
            "protect_response",
            py::bytes(reinterpret_cast<const char*>(application.data()), application.size()));
    }

    Result<Bytes> open_response(const protocol::apdu::SecurityResponse& message) override {
        return call<Bytes>("open_response", protocol::apdu::SecurityResponse(message));
    }

    void reset() noexcept override {
        if (!Py_IsInitialized()) return;
#if PY_VERSION_HEX >= 0x030d0000
        if (Py_IsFinalizing()) return;
#else
        if (_Py_IsFinalizing()) return;
#endif
        py::gil_scoped_acquire acquire;
        try {
            auto method = py::get_override(this, "reset");
            if (method && !method().is_none())
                throw py::type_error("SecurityBackend.reset must return None");
        } catch (py::error_already_set& error) {
            // 清理异常不能越过 noexcept 边界，但也不能静默掩盖密钥材料清理失败。
            error.discard_as_unraisable("SecurityBackend.reset");
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
            py::error_already_set().discard_as_unraisable("SecurityBackend.reset");
        }
    }
};

void bind_security_type(py::module_& module) {
    py::class_<security::IBackend, PythonBackend, py::smart_holder>(module, "SecurityBackend")
        .def(py::init<>());
}

}  // namespace dlt698::python
