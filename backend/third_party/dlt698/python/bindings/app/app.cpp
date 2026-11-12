#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>

#include "async_server.hpp"
#include "bindings.hpp"
#include "events.hpp"

namespace dlt698::python {

/** @brief 创建托管实例；Python 回收时释放 GIL 再等待原生线程收尾。
 * @tparam T 托管类型。
 * @tparam Args 构造参数类型。
 * @param[in] args 转发给构造器的参数。
 * @return 共享拥有型句柄，不持有 Python callback。
 */
template <class T, class... Args>
std::shared_ptr<T> managed(Args&&... args) {
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), [](T* value) {
        // 析构不触及 Python；在 Python 业务线程回收时允许取消完成并行运行。
        if (PyGILState_Check()) {
            py::gil_scoped_release release;
            delete value;
        } else
            delete value;
    });
}

void bind_enums(py::module_& module) {
    py::enum_<app::ConnectionProfile>(module, "ConnectionProfile")
        .value("remote_public", app::ConnectionProfile::remote_public)
        .value("local_public", app::ConnectionProfile::local_public)
        .value("local_preset", app::ConnectionProfile::local_preset);
    py::enum_<app::ClientState>(module, "ClientState")
        .value("disconnected", app::ClientState::disconnected)
        .value("connecting", app::ClientState::connecting)
        .value("connected", app::ClientState::connected)
        .value("disconnecting", app::ClientState::disconnecting);
    py::enum_<app::ServerState>(module, "ServerState")
        .value("stopped", app::ServerState::stopped)
        .value("starting", app::ServerState::starting)
        .value("running", app::ServerState::running)
        .value("stopping", app::ServerState::stopping);
    py::enum_<session::Role>(module, "Role")
        .value("client", session::Role::client)
        .value("server", session::Role::server);
    py::enum_<session::State>(module, "SessionState")
        .value("disconnected", session::State::disconnected)
        .value("preconnected", session::State::preconnected)
        .value("associating", session::State::associating)
        .value("associated", session::State::associated)
        .value("releasing", session::State::releasing)
        .value("closed", session::State::closed);
    py::enum_<protocol::apdu::LinkRequestType>(module, "LinkRequestType")
        .value("login", protocol::apdu::LinkRequestType::login)
        .value("heartbeat", protocol::apdu::LinkRequestType::heartbeat)
        .value("logout", protocol::apdu::LinkRequestType::logout);
    py::enum_<protocol::link::AddressType>(module, "AddressType")
        .value("single", protocol::link::AddressType::single)
        .value("wildcard", protocol::link::AddressType::wildcard)
        .value("group", protocol::link::AddressType::group)
        .value("broadcast", protocol::link::AddressType::broadcast);
    py::enum_<transport::SerialParity>(module, "SerialParity")
        .value("none", transport::SerialParity::none)
        .value("odd", transport::SerialParity::odd)
        .value("even", transport::SerialParity::even);
    py::enum_<transport::SerialStopBits>(module, "SerialStopBits")
        .value("one", transport::SerialStopBits::one)
        .value("one_point_five", transport::SerialStopBits::one_point_five)
        .value("two", transport::SerialStopBits::two);
    py::enum_<transport::SerialFlowControl>(module, "SerialFlowControl")
        .value("none", transport::SerialFlowControl::none)
        .value("software", transport::SerialFlowControl::software)
        .value("hardware", transport::SerialFlowControl::hardware);
}

void bind_app(py::module_& module) {
    Struct<Event> event(module, "Event");
    event.field("kind", &Event::kind)
        .field("connection", &Event::connection)
        .field("bytes", &Event::bytes)
        .field("error", &Event::error)
        .field("timestamp", &Event::timestamp)
        .finish();
    py::class_<EventQueue, std::shared_ptr<EventQueue>>(module, "EventQueue")
        .def(py::init<std::size_t, std::size_t>(), py::arg("capacity") = 256,
             py::arg("byte_limit") = 1024 * 1024)
        .def("drain", &EventQueue::drain, py::arg("count") = 256)
        .def("close", &EventQueue::close)
        .def_property_readonly("dropped", &EventQueue::dropped);

    py::class_<service::Device, std::shared_ptr<service::Device>>(module, "Device")
        .def(py::init<service::DeviceOptions>(), py::arg("options") = service::DeviceOptions{})
        .def("set", [](service::Device& device, model::Oad attr,
                       model::Data value) { unwrap(device.set(attr, std::move(value))); })
        .def("set_element",
             [](service::Device& device, model::Oad attr, model::Data value) {
                 unwrap(device.set_element(attr, std::move(value)));
             })
        .def("get", &service::Device::get)
        .def("define", [](service::Device& device, service::ObjectSchema schema) {
            unwrap(device.define(std::move(schema)));
        });

    py::class_<app::Client, std::shared_ptr<app::Client>>(module, "NativeClient")
        .def(py::init([](app::ClientOptions options, std::shared_ptr<EventQueue> events) {
                 if (options.protocol.security_backend ||
                     options.protocol.security_backend_factory || options.protocol.calendar_clock)
                     throw py::value_error(
                         "Python protocol callbacks require a caller-driven Engine");
                 if (events) {
                     std::weak_ptr<EventQueue> weak = events;
                     options.diagnostic = [weak](const Error& error) {
                         if (auto q = weak.lock()) q->push(Event{"diagnostic", 0, {}, error});
                     };
                     options.traffic = [weak](const session::TrafficEvent& traffic) {
                         if (auto q = weak.lock()) q->push(traffic_event(0, traffic));
                     };
                 }
                 return managed<app::Client>(std::move(options));
             }),
             py::arg("options") = app::ClientOptions{}, py::arg("events") = nullptr)
        .def(
            "connect_tcp",
            [](app::Client& client, std::string host, std::uint16_t port,
               app::ConnectionProfile profile) {
                auto result = [&] {
                    py::gil_scoped_release release;
                    return client.connect_tcp(std::move(host), port, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("host"), py::arg("port"),
            py::arg("profile") = app::ConnectionProfile::remote_public)
        .def(
            "open_serial",
            [](app::Client& client, std::string path, unsigned baud,
               app::ConnectionProfile profile) {
                auto result = [&] {
                    py::gil_scoped_release release;
                    return client.open_serial(std::move(path), baud, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("path"), py::arg("baud") = 9600,
            py::arg("profile") = app::ConnectionProfile::local_public)
        .def(
            "open_serial_configured",
            [](app::Client& client, std::string path, transport::SerialOptions serial,
               transport::SerialLinkOptions link, app::ConnectionProfile profile) {
                if (link.set_transmit || link.async_drain)
                    throw py::value_error(
                        "Python serial hooks require a caller-driven Engine or SerialLinkChannel");
                auto result = [&] {
                    py::gil_scoped_release release;
                    return client.open_serial(std::move(path), serial, link, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("path"), py::arg("serial"), py::arg("link"),
            py::arg("profile") = app::ConnectionProfile::local_public)
        .def("get",
             [](app::Client& client, model::Oad attr) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.get(attr);
                 }();
                 return unwrap(std::move(result));
             })
        .def("get_list",
             [](app::Client& client, std::vector<model::Oad> attrs) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.get_list(std::move(attrs));
                 }();
                 return unwrap(std::move(result));
             })
        .def("get_record",
             [](app::Client& client, protocol::apdu::GetRecord record) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.get_record(std::move(record));
                 }();
                 return unwrap(std::move(result));
             })
        .def("get_record_list",
             [](app::Client& client, std::vector<protocol::apdu::GetRecord> records) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.get_record_list(std::move(records));
                 }();
                 return unwrap(std::move(result));
             })
        .def("set",
             [](app::Client& client, model::Oad attr, model::Data value) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.set(attr, std::move(value));
                 }();
                 return unwrap(std::move(result));
             })
        .def("set_list",
             [](app::Client& client, std::vector<protocol::apdu::SetAttribute> attrs) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.set_list(std::move(attrs));
                 }();
                 return unwrap(std::move(result));
             })
        .def("action",
             [](app::Client& client, model::Omd method, model::Data parameter) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.action(method, std::move(parameter));
                 }();
                 return unwrap(std::move(result));
             })
        .def("action_list",
             [](app::Client& client, std::vector<protocol::apdu::ActionMethod> methods) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.action_list(std::move(methods));
                 }();
                 return unwrap(std::move(result));
             })
        .def("request_disconnect", &app::Client::request_disconnect)
        .def("disconnect",
             [](app::Client& client) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return client.disconnect();
                 }();
                 unwrap(std::move(result));
             })
        .def_property_readonly("state", &app::Client::state);

    py::class_<app::Server, std::shared_ptr<app::Server>>(module, "NativeServer")
        .def(py::init([](std::shared_ptr<service::Device> device, app::ServerOptions options,
                         std::shared_ptr<EventQueue> events) {
                 if (options.protocol.security_backend ||
                     options.protocol.security_backend_factory || options.protocol.calendar_clock)
                     throw py::value_error(
                         "Python protocol callbacks require a caller-driven Engine");
                 if (events) {
                     std::weak_ptr<EventQueue> weak = events;
                     options.diagnostic = [weak](std::uint64_t id, const Error& error) {
                         if (auto q = weak.lock()) q->push(Event{"diagnostic", id, {}, error});
                     };
                     options.traffic = [weak](std::uint64_t id,
                                              const session::TrafficEvent& traffic) {
                         if (auto q = weak.lock()) q->push(traffic_event(id, traffic));
                     };
                 }
                 if (device) return managed<app::Server>(std::move(device), std::move(options));
                 return managed<app::Server>(std::move(options));
             }),
             py::arg("device") = nullptr, py::arg("options") = app::ServerOptions{},
             py::arg("events") = nullptr)
        .def("set", [](app::Server& server, model::Oad attr,
                       model::Data value) { unwrap(server.set(attr, std::move(value))); })
        .def_property_readonly("device", &app::Server::device)
        .def(
            "start_tcp",
            [](app::Server& server, std::string address, std::uint16_t port,
               app::ConnectionProfile profile) {
                auto result = [&] {
                    py::gil_scoped_release release;
                    return server.start_tcp(std::move(address), port, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("address"), py::arg("port"),
            py::arg("profile") = app::ConnectionProfile::remote_public)
        .def(
            "start_serial",
            [](app::Server& server, std::string path, unsigned baud,
               app::ConnectionProfile profile) {
                auto result = [&] {
                    py::gil_scoped_release release;
                    return server.start_serial(std::move(path), baud, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("path"), py::arg("baud") = 9600,
            py::arg("profile") = app::ConnectionProfile::local_public)
        .def(
            "start_serial_configured",
            [](app::Server& server, std::string path, transport::SerialOptions serial,
               transport::SerialLinkOptions link, app::ConnectionProfile profile) {
                if (link.set_transmit || link.async_drain)
                    throw py::value_error(
                        "Python serial hooks require a caller-driven Engine or SerialLinkChannel");
                auto result = [&] {
                    py::gil_scoped_release release;
                    return server.start_serial(std::move(path), serial, link, profile);
                }();
                unwrap(std::move(result));
            },
            py::arg("path"), py::arg("serial"), py::arg("link"),
            py::arg("profile") = app::ConnectionProfile::local_public)
        .def("request_stop", &app::Server::request_stop)
        .def("stop",
             [](app::Server& server) {
                 auto result = [&] {
                     py::gil_scoped_release release;
                     return server.stop();
                 }();
                 unwrap(std::move(result));
             })
        .def_property_readonly("state", &app::Server::state)
        .def_property_readonly("local_port", &app::Server::local_port)
        .def_property_readonly("connections", &app::Server::connections);

    Struct<ServerOperation> operation(module, "ServerOperation");
    operation.field("token", &ServerOperation::token)
        .field("error", &ServerOperation::error)
        .finish();
    py::class_<ServerRunner, std::shared_ptr<ServerRunner>>(module, "ServerRunner")
        .def(py::init([](std::shared_ptr<app::Server> server) {
                 return managed<ServerRunner>(std::move(server));
             }),
             py::arg("server"))
        .def("start_tcp", &ServerRunner::start_tcp, py::arg("address"), py::arg("port"),
             py::arg("profile") = app::ConnectionProfile::remote_public)
        .def("start_serial", &ServerRunner::start_serial, py::arg("path"), py::arg("baud") = 9600,
             py::arg("profile") = app::ConnectionProfile::local_public)
        .def("start_serial_configured", &ServerRunner::start_serial_configured, py::arg("path"),
             py::arg("serial"), py::arg("link"),
             py::arg("profile") = app::ConnectionProfile::local_public)
        .def("stop", &ServerRunner::stop)
        .def("poll", &ServerRunner::poll);
}
}  // namespace dlt698::python
