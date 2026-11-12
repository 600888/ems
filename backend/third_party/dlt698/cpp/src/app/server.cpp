#include <atomic>
#include <dlt698/app/server.hpp>
#include <dlt698/service/service.hpp>
#include <dlt698/transport/memory.hpp>
#include <map>
#include <mutex>
#include <thread>

#include "detail.hpp"

namespace dlt698::app {
namespace {
struct Status {
    std::atomic<ServerState> state{ServerState::stopped};
    std::atomic<std::uint16_t> port{0};
    std::atomic<std::size_t> connections{0};
};

/** @brief 独立于公开句柄的单次运行，所有连接表操作均在管理执行器串行处理。 */
struct Run : std::enable_shared_from_this<Run> {
    std::shared_ptr<transport::IoRuntime> runtime = std::make_shared<transport::IoRuntime>();
    std::shared_ptr<IExecutor> manager = runtime->executor();
    std::shared_ptr<transport::TcpListener> listener;
    std::shared_ptr<Status> status;
    std::shared_ptr<service::ObjectRegistry> objects;
    ServerOptions options;
    session::SessionOptions protocol;
    std::map<std::uint64_t, std::shared_ptr<session::Session>> sessions;
    std::uint64_t next_id = 1;
    std::atomic<bool> stopping{false};
    std::thread worker;

    Run(std::shared_ptr<Status> published, std::shared_ptr<service::ObjectRegistry> registry,
        ServerOptions config, ConnectionProfile profile)
        : status(std::move(published)), objects(std::move(registry)), options(std::move(config)) {
        protocol = options.protocol;
        protocol.role = session::Role::server;
        protocol.require_login = profile == ConnectionProfile::remote_public;
        protocol.preset_association = profile == ConnectionProfile::local_preset;
        protocol.heartbeat_seconds = protocol.require_login ? options.heartbeat_seconds : 0;
        // 在绑定 socket 前复用 Session 的完整配置校验，失败不启动线程或留下监听。
        auto valid = session::validate_options(protocol);
        if (!valid) throw std::invalid_argument(valid.error().context);
    }

    ~Run() {
        if (!worker.joinable()) return;
        if (worker.get_id() == std::this_thread::get_id())
            detail::JoinQueue::instance().push(std::move(worker));
        else
            worker.join();
    }

    void notify(std::uint64_t id, const Error& error) noexcept {
        try {
            if (options.diagnostic) options.diagnostic(id, error);
        } catch (...) {
        }
    }

    Result<void> attach(std::shared_ptr<transport::IChannel> channel) {
        const auto id = next_id++;
        try {
            auto session =
                std::make_shared<session::Session>(channel, runtime->executor(), protocol);
            sessions.emplace(id, session);
            status->connections = sessions.size();
            const std::weak_ptr<Run> weak = shared_from_this();
            // 每个会话捕获独立 ID；观察器不持有 Run，避免回调与托管线程互相保活。
            if (options.traffic)
                session->set_traffic_handler(
                    [handler = options.traffic, id](const session::TrafficEvent& event) {
                        handler(id, event);
                    });
            session->set_close_handler([weak, id](const Error& reason) {
                if (const auto self = weak.lock())
                    self->manager->post([weak, id, reason] {
                        const auto self = weak.lock();
                        if (!self) return;
                        self->sessions.erase(id);
                        self->status->connections = self->sessions.size();
                        if (!self->stopping) self->notify(id, reason);
                    });
            });
            session->set_diagnostic_handler([weak, id](const Error& error) {
                if (const auto self = weak.lock()) self->notify(id, error);
            });
            service::ServerService service(session, objects);
            session->start();
            if (protocol.require_login)
                session->async_link(protocol::apdu::LinkRequestType::login,
                                    options.heartbeat_seconds, [weak, id](auto result) {
                                        if (const auto self = weak.lock())
                                            if (!result) self->notify(id, result.error());
                                    });
            return {};
        } catch (const std::exception& error) {
            sessions.erase(id);
            status->connections = sessions.size();
            channel->close();
            return Error{ErrorCode::invalid_value, 0, error.what()};
        }
    }

    void accept() {
        if (stopping) return;
        const std::weak_ptr<Run> weak = shared_from_this();
        listener->async_accept([weak](auto result) mutable {
            const auto self = weak.lock();
            if (!self) return;
            // 监听器与各会话有独立 strand；统一切到管理执行器更新连接表。
            self->manager->post([weak, result = std::move(result)]() mutable {
                const auto self = weak.lock();
                if (!self) return;
                if (self->stopping) {
                    if (result) result.value()->close();
                    return;
                }
                if (!result) {
                    self->notify(0, result.error());
                    self->request_stop();
                    return;
                }
                auto channel = std::move(result).value();
                if (self->sessions.size() >= self->options.max_connections) {
                    channel->close();
                    self->notify(0, {ErrorCode::resource_limit, 0, "server connections"});
                } else {
                    auto attached = self->attach(std::move(channel));
                    if (!attached) self->notify(0, attached.error());
                }
                self->accept();
            });
        });
    }

    void request_stop() {
        if (stopping.exchange(true)) return;
        status->state = ServerState::stopping;
        const auto self = shared_from_this();
        manager->post([self] {
            if (self->listener) self->listener->close();
            for (const auto& entry : self->sessions) entry.second->close();
            // 自然排空会话关闭、socket 取消及计时器取消；所有完成通知都属于在途工作。
            self->runtime->finish();
        });
    }

    void start_worker() {
        // 先初始化回收器，保证回调内释放最后句柄时无需从析构路径创建线程。
        (void)detail::JoinQueue::instance();
        const auto self = shared_from_this();
        worker = std::thread([self] {
            self->runtime->run();
            self->sessions.clear();
            self->listener.reset();
            self->status->connections = 0;
            self->status->port = 0;
            self->status->state = ServerState::stopped;
        });
        auto expected = ServerState::starting;
        status->state.compare_exchange_strong(expected, ServerState::running);
    }
};
}  // namespace

struct Server::Impl {
    std::shared_ptr<service::Device> device;
    ServerOptions options;
    std::shared_ptr<Status> status = std::make_shared<Status>();
    std::mutex operation;
    std::mutex mutex;
    std::shared_ptr<Run> run;

    Impl(std::shared_ptr<service::Device> data, ServerOptions config)
        : device(std::move(data)), options(std::move(config)) {
        if (!device || !options.max_connections) throw std::invalid_argument("server options");
    }

    std::shared_ptr<Run> current() {
        std::lock_guard<std::mutex> lock(mutex);
        return run;
    }

    Result<void> start(ConnectionProfile profile, const std::function<Result<void>(Run&)>& setup) {
        if (profile != ConnectionProfile::remote_public &&
            profile != ConnectionProfile::local_public &&
            profile != ConnectionProfile::local_preset)
            return Error{ErrorCode::invalid_value, 0, "invalid connection profile"};
        std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
        if (!lock || status->state != ServerState::stopped)
            return Error{ErrorCode::busy, 0, "server already running or changing state"};
        auto previous = current();
        if (previous && previous->worker.joinable()) previous->worker.join();
        std::shared_ptr<Run> next;
        try {
            next = std::make_shared<Run>(status, device->objects(), options, profile);
            {
                std::lock_guard<std::mutex> guard(mutex);
                run = next;
                status->state = ServerState::starting;
            }
            auto result = setup(*next);
            if (!result) {
                next->request_stop();
                next->runtime->run();
                status->connections = 0;
                status->port = 0;
                status->state = ServerState::stopped;
                return result.error();
            }
            next->start_worker();
            return {};
        } catch (const std::exception& error) {
            if (next) {
                next->request_stop();
                if (!next->worker.joinable()) next->runtime->run();
            }
            status->connections = 0;
            status->port = 0;
            status->state = ServerState::stopped;
            return Error{ErrorCode::invalid_value, 0, error.what()};
        }
    }
};

Server::Server(ServerOptions options)
    : Server(std::make_shared<service::Device>(), std::move(options)) {}

Server::Server(std::shared_ptr<service::Device> device, ServerOptions options)
    : impl_(std::make_shared<Impl>(std::move(device), std::move(options))) {}

Server::~Server() {
    const auto run = impl_->current();
    if (!run) return;
    run->request_stop();
    if (!run->manager->is_current() && run->worker.joinable()) run->worker.join();
}

Result<void> Server::set(model::Oad attribute, model::Data value) {
    return impl_->device->set(attribute, std::move(value));
}

std::shared_ptr<service::Device> Server::device() const { return impl_->device; }

Result<void> Server::start_tcp(std::string address, std::uint16_t port, ConnectionProfile profile) {
    const auto impl = impl_;
    return impl->start(profile, [address = std::move(address), port](Run& run) {
        auto bound =
            transport::TcpListener::listen(run.runtime, address, port, run.options.channel, true);
        if (!bound) return Result<void>{bound.error()};
        run.listener = std::move(bound).value();
        run.status->port = run.listener->local_port();
        run.manager->post([self = run.shared_from_this()] { self->accept(); });
        return Result<void>{};
    });
}

Result<void> Server::start_serial(std::string path, unsigned baud, ConnectionProfile profile) {
    transport::SerialOptions serial;
    serial.baud_rate = baud;
    serial.channel = impl_->options.channel;
    return start_serial(std::move(path), serial, {}, profile);
}

Result<void> Server::start_serial(std::string path, transport::SerialOptions serial,
                                  transport::SerialLinkOptions link, ConnectionProfile profile) {
    const auto impl = impl_;
    return impl->start(profile, [path = std::move(path), serial, link](Run& run) mutable {
        // 字格式决定物理位数；1.5 停止位无法用当前整数位模型准确表达，明确拒绝。
        if (serial.stop_bits == transport::SerialStopBits::one_point_five)
            return Result<void>{Error{ErrorCode::unsupported_service, 0, "1.5 serial stop bits"}};
        link.baud_rate = serial.baud_rate;
        link.bits_per_character = 1 + serial.data_bits +
                                  (serial.parity == transport::SerialParity::none ? 0 : 1) +
                                  (serial.stop_bits == transport::SerialStopBits::two ? 2 : 1);
        if (link.set_transmit && !link.async_drain)
            return Result<void>{Error{ErrorCode::invalid_value, 0, "RS-485 drain required"}};
        auto opened = transport::SerialChannel::open(run.runtime, path, serial);
        if (!opened) return Result<void>{opened.error()};
        auto raw = std::move(opened).value();
        try {
            auto channel = transport::SerialLinkChannel::wrap(raw, run.runtime->executor(), link);
            return run.attach(std::move(channel));
        } catch (...) {
            raw->close();
            throw;
        }
    });
}

void Server::request_stop() {
    if (const auto run = impl_->current()) {
        if (impl_->status->state != ServerState::stopped) run->request_stop();
    }
}

Result<void> Server::stop() {
    const auto impl = impl_;
    const auto run = impl->current();
    if (run && run->manager->is_current())
        return Error{ErrorCode::busy, 0, "use request_stop in server callback"};
    std::unique_lock<std::mutex> lock(impl->operation, std::try_to_lock);
    if (!lock) return Error{ErrorCode::busy, 0, "server start/stop in progress"};
    const auto active = impl->current();
    if (active) {
        if (impl->status->state != ServerState::stopped) active->request_stop();
        if (active->worker.joinable()) active->worker.join();
        std::lock_guard<std::mutex> guard(impl->mutex);
        impl->run.reset();
    }
    return {};
}

ServerState Server::state() const noexcept { return impl_->status->state.load(); }

std::uint16_t Server::local_port() const noexcept { return impl_->status->port.load(); }

std::size_t Server::connections() const noexcept { return impl_->status->connections.load(); }
}  // namespace dlt698::app
