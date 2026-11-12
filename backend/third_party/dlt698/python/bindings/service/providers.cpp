#include <pybind11/trampoline_self_life_support.h>

#include <dlt698/service/memory_records.hpp>

#include "callbacks.hpp"
#include "engine.hpp"

namespace dlt698::python {
// Python provider 只用于显式驱动 Engine。目录在锁外调用，Python 回调不能重入驱动器。
class PythonProvider : public service::IObjectProvider, public py::trampoline_self_life_support {
   public:
    service::ObjectValue read(const model::Oad& attribute) override {
        py::gil_scoped_acquire acquire;
        auto method = py::get_override(this, "read");
        if (!method) throw std::runtime_error("ObjectProvider.read must be implemented");
        // Python 可以保存参数；显式复制，不能借用目录调用栈上的 OAD 或 Data。
        return py::cast<service::ObjectValue>(method(py::cast(model::Oad(attribute))));
    }

    protocol::apdu::RecordResult read_record(const protocol::apdu::GetRecord& query) override {
        py::gil_scoped_acquire acquire;
        auto method = py::get_override(this, "read_record");
        if (!method) return service::IObjectProvider::read_record(query);
        return py::cast<protocol::apdu::RecordResult>(
            method(py::cast(protocol::apdu::GetRecord(query))));
    }

    std::uint8_t write(const model::Oad& attribute, const model::Data& value) override {
        py::gil_scoped_acquire acquire;
        auto method = py::get_override(this, "write");
        if (!method) return service::IObjectProvider::write(attribute, value);
        return integer<std::uint8_t>(
            method(py::cast(model::Oad(attribute)), py::cast(model::Data(value))));
    }

    service::ActionValue invoke(const model::Omd& method, const model::Data& parameter) override {
        py::gil_scoped_acquire acquire;
        auto callback = py::get_override(this, "invoke");
        if (!callback) return service::IObjectProvider::invoke(method, parameter);
        return py::cast<service::ActionValue>(
            callback(py::cast(model::Omd(method)), py::cast(model::Data(parameter))));
    }
};

// 代理扩展同样只在调用线程驱动；请求与 TSA 均拥有内存，完成函数遵守一次性交付。
class PythonProxy : public service::IProxyProvider, public py::trampoline_self_life_support {
   public:
    Cancel async_request(model::Tsa server, protocol::apdu::Apdu request,
                         Handler handler) override {
        py::gil_scoped_acquire acquire;
        auto callback = py::get_override(this, "async_request");
        if (!callback) {
            handler(Error{ErrorCode::unsupported_service, 0, "ProxyProvider.async_request"});
            return {};
        }
        return invoke_backend<protocol::apdu::Apdu>(callback, std::move(handler), std::move(server),
                                                    std::move(request));
    }
};

void bind_expert(py::module_& module) {
    py::class_<TransJob>(module, "TransJob")
        .def_readonly("token", &TransJob::token)
        .def_readonly("request", &TransJob::request);
    py::class_<TransBridge, std::shared_ptr<TransBridge>>(module, "TransBridge")
        .def(py::init<std::size_t, std::size_t>(), py::arg("max_pending") = 128,
             py::arg("max_bytes") = 1024 * 1024)
        .def("drain", &TransBridge::drain)
        .def(
            "complete",
            [](TransBridge& bridge, std::uint64_t token,
               std::variant<protocol::apdu::ProxyTransResponse, Error> result) {
                return std::visit(
                    [&](auto value) { return bridge.complete(token, std::move(value)); },
                    std::move(result));
            },
            py::arg("token"), py::arg("result"))
        .def("close", &TransBridge::close);
    py::class_<service::IObjectProvider, PythonProvider, py::smart_holder>(module, "ObjectProvider")
        .def(py::init<>())
        .def("read", &service::IObjectProvider::read)
        .def("read_record", &service::IObjectProvider::read_record)
        .def("write", &service::IObjectProvider::write)
        .def("invoke", &service::IObjectProvider::invoke);
    py::class_<service::MemoryObject, service::IObjectProvider, py::smart_holder>(module,
                                                                                  "MemoryObject")
        .def(py::init<>())
        .def("set", &service::MemoryObject::set)
        .def(
            "bind_method",
            [](service::MemoryObject& object, std::uint8_t method,
               std::function<service::ActionValue(model::Omd, model::Data)> callback) {
                if (!callback) throw py::type_error("method callback is required");
                // 参数按值转入 Python；业务回调保存它们也不会引用临时请求栈。
                object.bind_method(method,
                                   [callback = std::move(callback)](const model::Omd& omd,
                                                                    const model::Data& parameter) {
                                       return callback(model::Omd(omd), model::Data(parameter));
                                   });
            },
            py::arg("method"), py::arg("callback"))
        .def(
            "bind_record",
            [](service::MemoryObject& object, std::uint8_t attribute,
               std::function<protocol::apdu::RecordResult(protocol::apdu::GetRecord)> callback) {
                if (!callback) throw py::type_error("record callback is required");
                object.bind_record(attribute, [callback = std::move(callback)](
                                                  const protocol::apdu::GetRecord& query) {
                    return callback(protocol::apdu::GetRecord(query));
                });
            },
            py::arg("attribute"), py::arg("callback"));
    py::class_<service::ObjectRegistry, std::shared_ptr<service::ObjectRegistry>>(module,
                                                                                  "ObjectRegistry")
        .def(py::init<>())
        .def("register_object",
             [](service::ObjectRegistry& registry, service::ObjectSchema schema,
                std::shared_ptr<service::IObjectProvider> provider) {
                 unwrap(registry.register_object(std::move(schema), std::move(provider)));
             })
        .def("read", &service::ObjectRegistry::read)
        .def("read_record", &service::ObjectRegistry::read_record)
        .def("write", &service::ObjectRegistry::write)
        .def("invoke", &service::ObjectRegistry::invoke);
    Struct<service::RecordLimits> limits(module, "RecordLimits");
    limits.field("max_rows", &service::RecordLimits::max_rows)
        .field("max_result_rows", &service::RecordLimits::max_result_rows)
        .field("max_columns", &service::RecordLimits::max_columns)
        .field("max_snapshot_bytes", &service::RecordLimits::max_snapshot_bytes)
        .finish();
    py::class_<service::MemoryRecords, service::IObjectProvider, py::smart_holder>(module,
                                                                                   "MemoryRecords")
        .def_static(
            "create",
            [](std::uint16_t oi, std::vector<model::Oad> columns,
               const standard::DeviceLayout& layout, const Limits& limits,
               const service::RecordLimits& records) {
                return unwrap(service::MemoryRecords::create(oi, std::move(columns), layout, limits,
                                                             records));
            },
            py::arg("oi"), py::arg("columns") = std::vector<model::Oad>{},
            py::arg("layout") = standard::DeviceLayout{}, py::arg("limits") = Limits{},
            py::arg("record_limits") = service::RecordLimits{})
        .def("replace_rows",
             [](service::MemoryRecords& records, std::vector<protocol::apdu::RecordRow> rows) {
                 unwrap(records.replace_rows(std::move(rows)));
             });
    module.def("make_object_schema",
               [](std::uint16_t oi, const std::vector<std::uint8_t>& attributes) {
                   return unwrap(service::make_object_schema(oi, attributes));
               });
    module.def(
        "register_standard_object",
        [](service::ObjectRegistry& registry, std::uint16_t oi,
           const std::vector<std::uint8_t>& attributes,
           std::shared_ptr<service::IObjectProvider> provider, const standard::DeviceLayout& layout,
           const Limits& limits) {
            unwrap(service::register_standard_object(registry, oi, attributes, std::move(provider),
                                                     layout, limits));
        },
        py::arg("registry"), py::arg("oi"), py::arg("attributes"), py::arg("provider"),
        py::arg("layout") = standard::DeviceLayout{}, py::arg("limits") = Limits{});

    Struct<security::AuthenticationResult> authentication(module, "AuthenticationResult");
    authentication.field("result", &security::AuthenticationResult::result)
        .field("security", &security::AuthenticationResult::security)
        .finish();

    // 后端仅交给显式驱动 Engine；托管后台线程入口不接受 Python 安全后端。

    py::class_<session::Session, std::shared_ptr<session::Session>>(module, "SessionHandle")
        .def_property_readonly("state", &session::Session::state)
        .def("cancel", &session::Session::cancel)
        .def("request_close", &session::Session::close)
        .def("set_access_demand", &session::Session::set_access_demand);
    py::class_<service::IProxyProvider, PythonProxy, py::smart_holder>(module, "ProxyProvider")
        .def(py::init<>())
        .def(
            "async_request",
            [](service::IProxyProvider& provider, model::Tsa server, protocol::apdu::Apdu request,
               py::handle callback) {
                return provider.async_request(std::move(server), std::move(request),
                                              result_callback<protocol::apdu::Apdu>(callback));
            },
            py::arg("server"), py::arg("request"), py::arg("callback"));
    py::class_<service::ProxyRouter, service::IProxyProvider, py::smart_holder>(module,
                                                                                "ProxyRouter")
        .def(py::init<>())
        .def("bind", [](service::ProxyRouter& router, model::Tsa server,
                        std::shared_ptr<session::Session> session) {
            unwrap(router.bind(std::move(server), std::move(session)));
        });
    Struct<service::AdvancedServiceOptions> advanced(module, "AdvancedServiceOptions");
    advanced
        .field("default_read_delay_seconds",
               &service::AdvancedServiceOptions::default_read_delay_seconds)
        .field("default_proxy_timeout_seconds",
               &service::AdvancedServiceOptions::default_proxy_timeout_seconds)
        .field("limits", &service::AdvancedServiceOptions::limits)
        .field("proxy", &service::AdvancedServiceOptions::proxy)
        .finish();
    Struct<service::ProbeOptions>(module, "ProbeOptions")
        .field("batch_size", &service::ProbeOptions::batch_size)
        .field("layout", &service::ProbeOptions::layout)
        .field("limits", &service::ProbeOptions::limits)
        .finish();
    Struct<service::PointResult>(module, "PointResult")
        .field("attribute", &service::PointResult::attribute)
        .field("outcome", &service::PointResult::outcome)
        .field("schema_checked", &service::PointResult::schema_checked)
        .field("validation_error", &service::PointResult::validation_error)
        .finish();
    module.def(
        "async_probe_points",
        [](std::shared_ptr<session::Session> session, standard::Capabilities capabilities,
           std::vector<model::Oad> attributes, service::ProbeOptions options, py::handle callback) {
            service::async_probe_points(
                std::move(session), std::move(capabilities), std::move(attributes),
                std::move(options), result_callback<std::vector<service::PointResult>>(callback));
        },
        py::arg("session"), py::arg("capabilities"), py::arg("attributes"), py::arg("options"),
        py::arg("callback"));
    Struct<Completion> completion(module, "Completion");
    completion.field("token", &Completion::token)
        .field("connection", &Completion::connection)
        .field("kind", &Completion::kind)
        .field("error", &Completion::error)
        .field("message", &Completion::message)
        .field("state", &Completion::state)
        .field("traffic", &Completion::traffic)
        .field("points", &Completion::points)
        .finish();
    py::class_<Engine, std::shared_ptr<Engine>>(module, "Engine")
        .def(
            py::init<session::SessionOptions, std::shared_ptr<service::ObjectRegistry>, std::size_t,
                     service::AdvancedServiceOptions, std::size_t, std::shared_ptr<TransBridge>>(),
            py::arg("options") = session::SessionOptions{}, py::arg("objects") = nullptr,
            py::arg("queue_limit") = 1024, py::arg("advanced") = service::AdvancedServiceOptions{},
            py::arg("queue_bytes") = 16 * 1024 * 1024, py::arg("transparent") = nullptr)
        .def("connect_tcp", &Engine::connect_tcp, py::arg("host"), py::arg("port"),
             py::arg("profile") = app::ConnectionProfile::remote_public,
             py::arg("channel") = transport::ChannelOptions{}, py::arg("role") = py::none())
        .def("open_serial", &Engine::open_serial, py::arg("path"),
             py::arg("serial") = transport::SerialOptions{},
             py::arg("link") = transport::SerialLinkOptions{},
             py::arg("profile") = app::ConnectionProfile::local_public,
             py::arg("role") = session::Role::client)
        .def("listen", &Engine::listen, py::arg("address"), py::arg("port"),
             py::arg("profile") = app::ConnectionProfile::remote_public,
             py::arg("max_connections") = 16, py::arg("role") = py::none())
        .def("poll", &Engine::poll, py::arg("budget") = 0.001)
        .def("connect", &Engine::connect, py::arg("connection") = 1)
        .def("link", &Engine::link, py::arg("type"), py::arg("heartbeat_seconds") = 0,
             py::arg("connection") = 1)
        .def("release", &Engine::release, py::arg("connection") = 1)
        .def("probe_points", &Engine::probe_points, py::arg("capabilities"), py::arg("attributes"),
             py::arg("options") = service::ProbeOptions{}, py::arg("connection") = 1)
        .def("get", &Engine::get, py::arg("attributes"), py::arg("list") = false,
             py::arg("connection") = 1)
        .def("set", &Engine::set, py::arg("attributes"), py::arg("list") = false,
             py::arg("connection") = 1)
        .def("action", &Engine::action, py::arg("methods"), py::arg("list") = false,
             py::arg("connection") = 1)
        .def("get_record", &Engine::get_record, py::arg("records"), py::arg("list") = false,
             py::arg("connection") = 1)
        .def("exchange", &Engine::exchange, py::arg("request"), py::arg("connection") = 1)
        .def("cancel", &Engine::cancel, py::arg("connection") = 1)
        .def("close", &Engine::close)
        .def("session_at", &Engine::session_at, py::arg("connection") = 1);
}
}  // namespace dlt698::python
