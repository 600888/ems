#include <pybind11/trampoline_self_life_support.h>

#include <dlt698/service/advanced.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <dlt698/transport/tcp.hpp>

#include "callbacks.hpp"
#include "events.hpp"
#include "trans.hpp"

namespace dlt698::python {
// 这些扩展点由应用在同一线程排队驱动；trampoline 保活 Python 子类，不创建工作线程。
class PythonTimer : public ITimer, public py::trampoline_self_life_support {
   public:
    void cancel() override {
        py::gil_scoped_acquire acquire;
        try {
            auto method = py::get_override(this, "cancel");
            if (!method) throw py::type_error("ITimer.cancel must be implemented");
            method();
        } catch (py::error_already_set& error) {
            // cancel 也会从原生析构路径执行，异常只能报告，不能越过析构边界。
            error.discard_as_unraisable("ITimer.cancel");
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
            py::error_already_set().discard_as_unraisable("ITimer.cancel");
        }
    }
};

class PythonExecutor : public IExecutor, public py::trampoline_self_life_support {
   public:
    void post(Task task) override {
        PYBIND11_OVERRIDE_PURE(void, IExecutor, post, std::move(task));
    }

    std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) override {
        py::gil_scoped_acquire acquire;
        auto method = py::get_override(this, "schedule");
        if (!method) throw std::runtime_error("IExecutor.schedule must be implemented");
        auto timer = method(std::chrono::duration<double>(delay).count(), std::move(task))
                         .cast<std::shared_ptr<ITimer>>();
        if (!timer) throw py::value_error("schedule must return ITimer");
        return timer;
    }

    Clock::time_point now() const noexcept override {
        py::gil_scoped_acquire acquire;
        try {
            auto method = py::get_override(this, "now");
            if (!method) throw py::type_error("IExecutor.now must be implemented");
            double seconds = method().cast<double>();
            return Clock::time_point(clock_duration(seconds));
        } catch (py::error_already_set& error) {
            // 原生查询为 noexcept，不能让 Python 异常越过 ABI 导致进程终止。
            error.discard_as_unraisable("IExecutor.now");
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
            py::error_already_set().discard_as_unraisable("IExecutor.now");
        }
        return Clock::time_point{};
    }

    bool is_current() const noexcept override {
        py::gil_scoped_acquire acquire;
        try {
            auto method = py::get_override(this, "is_current");
            if (!method) throw py::type_error("IExecutor.is_current must be implemented");
            return method().cast<bool>();
        } catch (py::error_already_set& error) {
            error.discard_as_unraisable("IExecutor.is_current");
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
            py::error_already_set().discard_as_unraisable("IExecutor.is_current");
        }
        return false;
    }
};

class PythonChannel : public transport::IChannel, public py::trampoline_self_life_support {
   public:
    void async_read(ReadHandler handler) override {
        invoke<Bytes>("async_read", std::move(handler));
    }

    void async_write(Bytes bytes, WriteHandler handler) override {
        // 原生运行时驱动时会释放 GIL，实参转换也属于 Python API，须先重新获取。
        py::gil_scoped_acquire acquire;
        invoke<void>("async_write", std::move(handler), bytes_to(bytes));
    }

    void close() override {
        py::gil_scoped_acquire acquire;
        try {
            auto method = py::get_override(this, "close");
            if (!method) throw py::type_error("IChannel.close must be implemented");
            method();
        } catch (py::error_already_set& error) {
            // 自定义 close 错误不得终止原生会话析构，交给 sys.unraisablehook 诊断。
            error.discard_as_unraisable("IChannel.close");
        } catch (const std::exception& error) {
            PyErr_SetString(PyExc_ValueError, error.what());
            py::error_already_set().discard_as_unraisable("IChannel.close");
        }
    }

   private:
    template <class T, class... Args>
    void invoke(const char* name, std::function<void(Result<T>)> handler, Args... args) {
        py::gil_scoped_acquire acquire;
        auto pending = std::make_shared<std::function<void(Result<T>)>>(std::move(handler));
        auto done = completion_function<T>([pending](Result<T> result) {
            auto call = std::move(*pending);
            *pending = {};
            if (call) call(std::move(result));
        });
        try {
            auto method = py::get_override(this, name);
            if (!method)
                throw py::type_error(std::string("IChannel.") + name + " must be implemented");
            method(std::move(args)..., done);
        } catch (const std::exception& error) {
            // 抛异常前若已经完成，不再第二次通知；晚到完成只消费空回调。
            auto call = std::move(*pending);
            *pending = {};
            if (call) call(Error{ErrorCode::io_error, 0, error.what()});
        }
    }
};

void bind_channels(py::module_& module) {
    module.def(
        "validate_session_options",
        [](const session::SessionOptions& options) { unwrap(session::validate_options(options)); },
        py::arg("options"));
    py::class_<ITimer, PythonTimer, py::smart_holder>(module, "ITimer")
        .def(py::init<>())
        .def("cancel", &ITimer::cancel);
    py::class_<IExecutor, PythonExecutor, py::smart_holder>(module, "IExecutor")
        .def(py::init<>())
        .def("post", &IExecutor::post, py::arg("task"))
        .def(
            "schedule",
            [](IExecutor& executor, double delay, IExecutor::Task task) {
                return executor.schedule(clock_duration(delay), std::move(task));
            },
            py::arg("delay"), py::arg("task"))
        .def("now",
             [](const IExecutor& executor) {
                 return std::chrono::duration<double>(executor.now().time_since_epoch()).count();
             })
        .def("is_current", &IExecutor::is_current);
    py::class_<ManualExecutor, IExecutor, py::smart_holder>(module, "ManualExecutor")
        .def(py::init<>())
        .def("run_ready", &ManualExecutor::run_ready)
        .def(
            "advance",
            [](ManualExecutor& executor, double elapsed) {
                executor.advance(clock_duration(elapsed));
            },
            py::arg("elapsed"));
    py::class_<transport::IoRuntime, std::shared_ptr<transport::IoRuntime>>(module, "IoRuntime")
        .def(py::init<>())
        .def("executor", &transport::IoRuntime::executor)
        .def(
            "run_for",
            [](transport::IoRuntime& runtime, double budget) {
                auto duration = milliseconds(budget);
                py::gil_scoped_release release;
                runtime.run_for(duration);
            },
            py::arg("budget") = 0.001)
        .def("stop", &transport::IoRuntime::stop)
        .def("finish", &transport::IoRuntime::finish)
        .def("restart", &transport::IoRuntime::restart);
    py::class_<transport::IChannel, PythonChannel, py::smart_holder>(module, "IChannel")
        .def(py::init<>())
        .def(
            "async_read",
            [](transport::IChannel& channel, py::handle callback) {
                channel.async_read(result_callback<Bytes>(callback));
            },
            py::arg("callback"))
        .def(
            "async_write",
            [](transport::IChannel& channel, Bytes bytes, py::handle callback) {
                channel.async_write(std::move(bytes), result_callback<void>(callback));
            },
            py::arg("data"), py::arg("callback"))
        .def("close", &transport::IChannel::close);
    Struct<transport::MemoryOptions>(module, "MemoryOptions")
        .field("read_chunk_bytes", &transport::MemoryOptions::read_chunk_bytes)
        .field("max_buffer_bytes", &transport::MemoryOptions::max_buffer_bytes)
        .field("max_pending_writes", &transport::MemoryOptions::max_pending_writes)
        .finish();
    py::class_<transport::MemoryChannel, transport::IChannel, py::smart_holder>(module,
                                                                                "MemoryChannel")
        .def_static("pair", &transport::MemoryChannel::pair, py::arg("executor"),
                    py::arg("options") = transport::MemoryOptions{});
    py::class_<transport::SerialChannel, transport::IChannel, py::smart_holder>(module,
                                                                                "SerialChannel")
        .def_static(
            "open",
            [](std::shared_ptr<transport::IoRuntime> runtime, const std::string& path,
               transport::SerialOptions options) {
                return unwrap(transport::SerialChannel::open(std::move(runtime), path, options));
            },
            py::arg("runtime"), py::arg("path"), py::arg("options") = transport::SerialOptions{});
    py::class_<transport::SerialLinkChannel, transport::IChannel, py::smart_holder>(
        module, "SerialLinkChannel")
        .def_static("wrap", &transport::SerialLinkChannel::wrap, py::arg("raw"),
                    py::arg("executor"), py::arg("options") = transport::SerialLinkOptions{});
    py::class_<transport::TcpChannel, transport::IChannel, py::smart_holder>(module, "TcpChannel")
        .def_static(
            "connect",
            [](std::shared_ptr<transport::IoRuntime> runtime, std::string host, std::uint16_t port,
               py::handle callback, transport::ChannelOptions options) {
                return transport::TcpChannel::connect(std::move(runtime), std::move(host), port,
                                                      result_callback<void>(callback), options);
            },
            py::arg("runtime"), py::arg("host"), py::arg("port"), py::arg("callback"),
            py::arg("options") = transport::ChannelOptions{});
    py::class_<transport::TcpListener, std::shared_ptr<transport::TcpListener>>(module,
                                                                                "TcpListener")
        .def_static(
            "listen",
            [](std::shared_ptr<transport::IoRuntime> runtime, const std::string& address,
               std::uint16_t port, transport::ChannelOptions options) {
                return unwrap(
                    transport::TcpListener::listen(std::move(runtime), address, port, options));
            },
            py::arg("runtime"), py::arg("address"), py::arg("port"),
            py::arg("options") = transport::ChannelOptions{})
        .def_property_readonly("local_port", &transport::TcpListener::local_port)
        .def(
            "async_accept",
            [](transport::TcpListener& listener, py::handle callback) {
                listener.async_accept(
                    result_callback<std::shared_ptr<transport::TcpChannel>>(callback));
            },
            py::arg("callback"))
        .def("close", &transport::TcpListener::close);

    auto cls =
        py::reinterpret_borrow<py::class_<session::Session, std::shared_ptr<session::Session>>>(
            module.attr("SessionHandle"));
    cls.def(py::init<std::shared_ptr<transport::IChannel>, std::shared_ptr<IExecutor>,
                     session::SessionOptions>(),
            py::arg("channel"), py::arg("executor"), py::arg("options") = session::SessionOptions{})
        .def("start", &session::Session::start)
        .def("in_executor_thread", &session::Session::in_executor_thread)
        .def(
            "async_connect",
            [](session::Session& session, py::handle callback) {
                session.async_connect(result_callback<protocol::apdu::ConnectResponse>(callback));
            },
            py::arg("callback"))
        .def(
            "async_link",
            [](session::Session& session, protocol::apdu::LinkRequestType type,
               std::uint16_t heartbeat, py::handle callback) {
                session.async_link(type, heartbeat,
                                   result_callback<protocol::apdu::LinkResponse>(callback));
            },
            py::arg("type"), py::arg("heartbeat_seconds"), py::arg("callback"))
        .def(
            "async_release",
            [](session::Session& session, py::handle callback) {
                session.async_release(result_callback<void>(callback));
            },
            py::arg("callback"))
        .def(
            "async_get",
            [](session::Session& session, std::vector<model::Oad> attributes, bool list,
               py::handle callback) {
                session.async_get(std::move(attributes), list,
                                  result_callback<protocol::apdu::GetResponse>(callback));
            },
            py::arg("attributes"), py::arg("list"), py::arg("callback"))
        .def(
            "async_set",
            [](session::Session& session, std::vector<protocol::apdu::SetAttribute> attributes,
               bool list, py::handle callback) {
                session.async_set(std::move(attributes), list,
                                  result_callback<protocol::apdu::SetResponse>(callback));
            },
            py::arg("attributes"), py::arg("list"), py::arg("callback"))
        .def(
            "async_action",
            [](session::Session& session, std::vector<protocol::apdu::ActionMethod> methods,
               bool list, py::handle callback) {
                session.async_action(std::move(methods), list,
                                     result_callback<protocol::apdu::ActionResponse>(callback));
            },
            py::arg("methods"), py::arg("list"), py::arg("callback"))
        .def(
            "async_get_record",
            [](session::Session& session, std::vector<protocol::apdu::GetRecord> records, bool list,
               py::handle callback) {
                session.async_get_record(
                    std::move(records), list,
                    result_callback<protocol::apdu::GetRecordResponse>(callback));
            },
            py::arg("records"), py::arg("list"), py::arg("callback"))
        .def(
            "async_exchange",
            [](session::Session& session, protocol::apdu::Apdu request, py::handle callback) {
                session.async_exchange(std::move(request),
                                       result_callback<protocol::apdu::Apdu>(callback));
            },
            py::arg("request"), py::arg("callback"))
        .def("set_state_handler", &session::Session::set_state_handler, py::arg("callback"))
        .def("set_acd_handler", &session::Session::set_acd_handler, py::arg("callback"))
        .def(
            "set_report_handler",
            [](session::Session& session,
               std::function<bool(protocol::apdu::ReportNotification)> callback) {
                // bool 返回值由业务明确决定是否确认；保存参数不会借用接收栈。
                session.set_report_handler(
                    callback ? session::Session::ReportHandler(
                                   [callback = std::move(callback)](
                                       const protocol::apdu::ReportNotification& message) {
                                       return callback(protocol::apdu::ReportNotification(message));
                                   })
                             : session::Session::ReportHandler{});
            },
            py::arg("callback"))
        .def(
            "set_follow_handler",
            [](session::Session& session,
               std::function<void(protocol::apdu::FollowReport)> callback) {
                session.set_follow_handler(
                    callback ? session::Session::FollowHandler(
                                   [callback = std::move(callback)](
                                       const protocol::apdu::FollowReport& message) {
                                       callback(protocol::apdu::FollowReport(message));
                                   })
                             : session::Session::FollowHandler{});
            },
            py::arg("callback"))
        .def(
            "set_diagnostic_handler",
            [](session::Session& session, std::function<void(Error)> callback) {
                session.set_diagnostic_handler(
                    callback ? session::Session::DiagnosticHandler(
                                   [callback = std::move(callback)](const Error& error) {
                                       callback(Error(error));
                                   })
                             : session::Session::DiagnosticHandler{});
            },
            py::arg("callback"))
        .def(
            "set_advanced_handler",
            [](session::Session& session, py::object callback) {
                if (callback.is_none()) {
                    session.set_advanced_handler({});
                    return;
                }
                if (!PyCallable_Check(callback.ptr()))
                    throw py::type_error("handler must be callable");
                session.set_advanced_handler(
                    [callback = std::move(callback)](protocol::apdu::Apdu request,
                                                     session::Session::ExchangeHandler complete) {
                        return invoke_backend<protocol::apdu::Apdu>(callback, std::move(complete),
                                                                    std::move(request));
                    });
            },
            py::arg("callback"))
        .def(
            "set_close_handler",
            [](session::Session& session, std::function<void(Error)> callback) {
                session.set_close_handler(
                    callback ? session::Session::CloseHandler(
                                   [callback = std::move(callback)](const Error& error) {
                                       callback(Error(error));
                                   })
                             : session::Session::CloseHandler{});
            },
            py::arg("callback"))
        .def(
            "set_traffic_handler",
            [](session::Session& session, std::function<void(Event)> callback) {
                session.set_traffic_handler(
                    callback ? session::TrafficHandler([callback = std::move(callback)](
                                                           const session::TrafficEvent& traffic) {
                        callback(traffic_event(0, traffic));
                    })
                             : session::TrafficHandler{});
            },
            py::arg("callback"));
    // 四种普通请求处理器均复制输入；空回调移除处理器，不隐式替换对象服务。
    cls.def(
        "set_request_handler",
        [](session::Session& session,
           std::function<protocol::apdu::GetResponse(protocol::apdu::GetRequest)> callback) {
            session.set_request_handler(
                callback ? session::Session::RequestHandler(
                               [callback = std::move(callback)](
                                   const protocol::apdu::GetRequest& request) {
                                   return callback(protocol::apdu::GetRequest(request));
                               })
                         : session::Session::RequestHandler{});
        },
        py::arg("callback"));
    cls.def(
        "set_record_handler",
        [](session::Session& session,
           std::function<protocol::apdu::GetRecordResponse(protocol::apdu::GetRecordRequest)>
               callback) {
            session.set_record_handler(
                callback ? session::Session::RecordRequestHandler(
                               [callback = std::move(callback)](
                                   const protocol::apdu::GetRecordRequest& request) {
                                   return callback(protocol::apdu::GetRecordRequest(request));
                               })
                         : session::Session::RecordRequestHandler{});
        },
        py::arg("callback"));
    cls.def(
        "set_set_handler",
        [](session::Session& session,
           std::function<protocol::apdu::SetResponse(protocol::apdu::SetRequest)> callback) {
            session.set_set_handler(callback ? session::Session::SetRequestHandler(
                                                   [callback = std::move(callback)](
                                                       const protocol::apdu::SetRequest& request) {
                                                       return callback(
                                                           protocol::apdu::SetRequest(request));
                                                   })
                                             : session::Session::SetRequestHandler{});
        },
        py::arg("callback"));
    cls.def(
        "set_action_handler",
        [](session::Session& session,
           std::function<protocol::apdu::ActionResponse(protocol::apdu::ActionRequest)> callback) {
            session.set_action_handler(
                callback ? session::Session::ActionRequestHandler(
                               [callback = std::move(callback)](
                                   const protocol::apdu::ActionRequest& request) {
                                   return callback(protocol::apdu::ActionRequest(request));
                               })
                         : session::Session::ActionRequestHandler{});
        },
        py::arg("callback"));
    module.def(
        "attach_services",
        [](std::shared_ptr<session::Session> session,
           std::shared_ptr<service::ObjectRegistry> objects) {
            service::ServerService normal(std::move(session), std::move(objects));
        },
        py::arg("session"), py::arg("objects"));
    module.def(
        "attach_advanced_services",
        [](std::shared_ptr<session::Session> session,
           std::shared_ptr<service::ObjectRegistry> objects, std::shared_ptr<IExecutor> executor,
           service::AdvancedServiceOptions options, std::shared_ptr<TransBridge> transparent) {
            if (transparent)
                options.trans =
                    [bridge = std::move(transparent)](
                        protocol::apdu::ProxyTransRequest request,
                        std::function<void(Result<protocol::apdu::ProxyTransResponse>)> complete) {
                        return bridge->submit(std::move(request), std::move(complete));
                    };
            service::AdvancedService advanced(std::move(session), std::move(objects),
                                              std::move(executor), std::move(options));
        },
        py::arg("session"), py::arg("objects"), py::arg("executor"),
        py::arg("options") = service::AdvancedServiceOptions{}, py::arg("transparent") = nullptr);
}
}  // namespace dlt698::python
