#include <atomic>
#include <dlt698/app/client.hpp>
#include <dlt698/transport/memory.hpp>
#include <optional>

#include "detail.hpp"

namespace dlt698::app {
namespace {
struct Startup {
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Result<void>> result;

    bool complete(Result<void> value) {
        // 建连、超时和取消可能竞争，只接受首个终结结果，迟到回调不能改写它。
        std::lock_guard<std::mutex> lock(mutex);
        if (result) return false;
        result = std::move(value);
        changed.notify_all();
        return true;
    }

    Result<void> wait() {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [this] { return result.has_value(); });
        return std::move(*result);
    }
};

struct ClientRun : std::enable_shared_from_this<ClientRun> {
    std::shared_ptr<transport::IoRuntime> runtime = std::make_shared<transport::IoRuntime>();
    std::shared_ptr<IExecutor> executor = runtime->executor();
    std::shared_ptr<std::atomic<ClientState>> state;
    std::shared_ptr<transport::IChannel> channel;
    std::shared_ptr<session::Session> session;
    std::unique_ptr<service::SyncClientService> service;
    std::shared_ptr<ITimer> phase_timer;
    Startup startup;
    ClientOptions options;
    session::SessionOptions protocol;
    std::atomic<bool> stopping{false};
    bool connecting = false;
    std::thread worker;

    ClientRun(std::shared_ptr<std::atomic<ClientState>> published, ClientOptions config,
              ConnectionProfile profile)
        : state(std::move(published)), options(std::move(config)), protocol(options.protocol) {
        protocol.role = session::Role::client;
        protocol.require_login = profile == ConnectionProfile::remote_public;
        protocol.preset_association = profile == ConnectionProfile::local_preset;
        protocol.heartbeat_seconds = 0;
        // 打开传输前复用完整协议校验，不用真实 socket 探测非法参数。
        auto valid = session::validate_options(protocol);
        if (!valid) throw std::invalid_argument(valid.error().context);
    }

    ~ClientRun() {
        if (!worker.joinable()) return;
        if (worker.get_id() == std::this_thread::get_id())
            detail::JoinQueue::instance().push(std::move(worker));
        else
            worker.join();
    }

    void notify(const Error& error) noexcept {
        try {
            if (options.diagnostic) options.diagnostic(error);
        } catch (...) {
        }
    }

    void request_stop() {
        if (stopping.exchange(true)) return;
        *state = ClientState::disconnecting;
        startup.complete(Error{ErrorCode::cancelled, 0, "client connection cancelled"});
        const auto self = shared_from_this();
        executor->post([self] {
            if (self->phase_timer) self->phase_timer->cancel();
            if (self->session) self->session->close();
            if (self->channel) self->channel->close();
            // 保留事件循环直到 DNS/socket/会话取消及等待完成全部排空。
            self->runtime->finish();
        });
    }

    void fail(Error error) {
        startup.complete(error);
        request_stop();
    }

    void arm(std::chrono::milliseconds timeout, const char* context) {
        if (phase_timer) phase_timer->cancel();
        const std::weak_ptr<ClientRun> weak = shared_from_this();
        phase_timer = executor->schedule(timeout, [weak, context] {
            if (const auto self = weak.lock()) self->fail({ErrorCode::timeout, 0, context});
        });
    }

    void connected() {
        if (phase_timer) phase_timer->cancel();
        auto expected = ClientState::connecting;
        // 不覆盖并发取消已发布的 disconnecting；同步调用只在全部成员准备完后开放。
        if (stopping || !state->compare_exchange_strong(expected, ClientState::connected)) {
            request_stop();
            return;
        }
        startup.complete({});
    }

    void associate(session::State value) {
        if (*state == ClientState::connected &&
            (value == session::State::preconnected || value == session::State::disconnected)) {
            // 对端释放/退出登录后不保留虚假的 connected 状态，也不自动重建关联。
            request_stop();
            notify({ErrorCode::not_associated, 0, "peer association ended"});
            return;
        }
        if (connecting || stopping) return;
        if (protocol.preset_association) {
            if (value == session::State::associated) {
                connecting = true;
                connected();
            }
            return;
        }
        if (value != session::State::preconnected) return;
        // LINK 就绪由状态观察器通知；只发起一次 CONNECT，不在运行线程同步等待自己。
        connecting = true;
        if (phase_timer) phase_timer->cancel();
        const std::weak_ptr<ClientRun> weak = shared_from_this();
        session->async_connect([weak](auto result) {
            const auto self = weak.lock();
            if (!self) return;
            if (!result)
                self->fail(result.error());
            else if (result.value().result != 0)
                self->fail(
                    {ErrorCode::association_failed, 0, "CONNECT rejected", result.value().result});
            else
                self->connected();
        });
    }

    void initialize() {
        if (stopping) return;
        try {
            if (phase_timer) phase_timer->cancel();
            session = std::make_shared<session::Session>(channel, runtime->executor(), protocol);
            service = std::make_unique<service::SyncClientService>(session);
            const std::weak_ptr<ClientRun> weak = shared_from_this();
            if (options.traffic) session->set_traffic_handler(options.traffic);
            session->set_close_handler([weak](const Error& error) {
                if (const auto self = weak.lock()) {
                    self->startup.complete(error);
                    const bool report = !self->stopping;
                    self->request_stop();
                    if (report) self->notify(error);
                }
            });
            session->set_diagnostic_handler([weak](const Error& error) {
                if (const auto self = weak.lock()) self->notify(error);
            });
            session->set_state_handler([weak](session::State value) {
                if (const auto self = weak.lock()) self->associate(value);
            });
            if (protocol.require_login) arm(options.login_timeout, "LINK login timeout");
            session->start();
        } catch (const std::exception& error) {
            fail({ErrorCode::invalid_value, 0, error.what()});
        }
    }

    void start_worker() {
        (void)detail::JoinQueue::instance();
        const auto self = shared_from_this();
        worker = std::thread([self] {
            self->runtime->run();
            *self->state = ClientState::disconnected;
        });
    }

    void finish() {
        request_stop();
        if (worker.joinable())
            worker.join();
        else {
            runtime->run();
            *state = ClientState::disconnected;
        }
    }
};
}  // namespace

struct Client::Impl {
    ClientOptions options;
    std::shared_ptr<std::atomic<ClientState>> state =
        std::make_shared<std::atomic<ClientState>>(ClientState::disconnected);
    std::mutex operation;
    std::mutex mutex;
    std::shared_ptr<ClientRun> run;

    explicit Impl(ClientOptions config) : options(std::move(config)) {
        const auto maximum = std::chrono::duration_cast<std::chrono::milliseconds>(
            IExecutor::Clock::duration::max());
        if (options.transport_timeout.count() <= 0 || options.login_timeout.count() <= 0 ||
            options.transport_timeout > maximum || options.login_timeout > maximum)
            throw std::invalid_argument("client stage timeout");
    }

    std::shared_ptr<ClientRun> current() {
        std::lock_guard<std::mutex> lock(mutex);
        return run;
    }

    Result<void> start(ConnectionProfile profile,
                       const std::function<Result<void>(ClientRun&)>& setup) {
        if (profile != ConnectionProfile::remote_public &&
            profile != ConnectionProfile::local_public &&
            profile != ConnectionProfile::local_preset)
            return Error{ErrorCode::invalid_value, 0, "invalid connection profile"};
        const auto previous = current();
        if (previous && previous->executor->is_current())
            return Error{ErrorCode::busy, 0, "connect in client callback"};
        std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
        if (!lock || *state != ClientState::disconnected)
            return Error{ErrorCode::busy, 0, "client already connected or changing state"};
        if (previous && previous->worker.joinable()) previous->worker.join();
        std::shared_ptr<ClientRun> next;
        try {
            next = std::make_shared<ClientRun>(state, options, profile);
            {
                std::lock_guard<std::mutex> guard(mutex);
                run = next;
                *state = ClientState::connecting;
            }
            auto result = setup(*next);
            if (!result) {
                next->finish();
                return result.error();
            }
            next->start_worker();
            result = next->startup.wait();
            if (!result) next->finish();
            return result;
        } catch (const std::exception& error) {
            if (next) next->finish();
            return Error{ErrorCode::invalid_value, 0, error.what()};
        }
    }

    template <class T, class Call>
    Result<T> invoke(Call call) {
        const auto active = current();
        if (active && active->executor->is_current())
            return Error{ErrorCode::busy, 0, "sync call in client callback"};
        if (!active || *state != ClientState::connected)
            return Error{ErrorCode::not_associated, 0, "client not connected"};
        // 每次请求保活整次运行；并发断开只关闭会话，不销毁正在等待的同步适配器。
        return call(*active->service);
    }
};

Client::Client(ClientOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {}

Client::~Client() {
    const auto run = impl_->current();
    if (!run) return;
    run->request_stop();
    if (!run->executor->is_current() && run->worker.joinable()) run->worker.join();
}

Result<void> Client::connect_tcp(std::string host, std::uint16_t port, ConnectionProfile profile) {
    if (host.empty() || !port) return Error{ErrorCode::invalid_value, 0, "TCP host/port"};
    const auto impl = impl_;
    return impl->start(profile, [host = std::move(host), port](ClientRun& run) {
        const std::weak_ptr<ClientRun> weak = run.shared_from_this();
        run.arm(run.options.transport_timeout, "DNS/TCP connect timeout");
        run.channel = transport::TcpChannel::connect(
            run.runtime, host, port,
            [weak](auto result) {
                if (const auto self = weak.lock())
                    self->executor->post([weak, result = std::move(result)] {
                        const auto self = weak.lock();
                        if (!self || self->stopping) return;
                        if (!result)
                            self->fail(result.error());
                        else
                            self->initialize();
                    });
            },
            run.options.channel);
        return Result<void>{};
    });
}

Result<void> Client::open_serial(std::string path, unsigned baud, ConnectionProfile profile) {
    transport::SerialOptions serial;
    serial.baud_rate = baud;
    serial.channel = impl_->options.channel;
    return open_serial(std::move(path), serial, {}, profile);
}

Result<void> Client::open_serial(std::string path, transport::SerialOptions serial,
                                 transport::SerialLinkOptions link, ConnectionProfile profile) {
    const auto impl = impl_;
    return impl->start(profile, [path = std::move(path), serial, link](ClientRun& run) mutable {
        // 与服务器采用同一串行字格式规则，不猜测硬件实际排空能力。
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
        run.channel = std::move(opened).value();
        run.channel =
            transport::SerialLinkChannel::wrap(run.channel, run.runtime->executor(), link);
        run.executor->post([self = run.shared_from_this()] { self->initialize(); });
        return Result<void>{};
    });
}

Result<service::ObjectValue> Client::get(model::Oad attribute) {
    const auto impl = impl_;
    return impl->invoke<service::ObjectValue>(
        [&](auto& service) { return service.get(attribute); });
}

Result<protocol::apdu::GetResponse> Client::get_list(std::vector<model::Oad> attributes) {
    const auto impl = impl_;
    return impl->invoke<protocol::apdu::GetResponse>(
        [&](auto& service) { return service.get_list(std::move(attributes)); });
}

Result<protocol::apdu::RecordResult> Client::get_record(protocol::apdu::GetRecord record) {
    const auto impl = impl_;
    return impl->invoke<protocol::apdu::RecordResult>(
        [&](auto& service) { return service.get_record(std::move(record)); });
}

Result<protocol::apdu::GetRecordResponse> Client::get_record_list(
    std::vector<protocol::apdu::GetRecord> records) {
    const auto impl = impl_;
    return impl->invoke<protocol::apdu::GetRecordResponse>(
        [&](auto& service) { return service.get_record_list(std::move(records)); });
}

Result<std::uint8_t> Client::set(model::Oad attribute, model::Data value) {
    const auto impl = impl_;
    return impl->invoke<std::uint8_t>(
        [&](auto& service) { return service.set(attribute, std::move(value)); });
}

Result<protocol::apdu::SetResponse> Client::set_list(
    std::vector<protocol::apdu::SetAttribute> attributes) {
    const auto impl = impl_;
    return impl->invoke<protocol::apdu::SetResponse>(
        [&](auto& service) { return service.set_list(std::move(attributes)); });
}

Result<service::ActionValue> Client::action(model::Omd method, model::Data parameter) {
    const auto impl = impl_;
    return impl->invoke<service::ActionValue>(
        [&](auto& service) { return service.action(method, std::move(parameter)); });
}

Result<protocol::apdu::ActionResponse> Client::action_list(
    std::vector<protocol::apdu::ActionMethod> methods) {
    const auto impl = impl_;
    return impl->invoke<protocol::apdu::ActionResponse>(
        [&](auto& service) { return service.action_list(std::move(methods)); });
}

void Client::request_disconnect() {
    if (const auto run = impl_->current()) {
        if (*impl_->state != ClientState::disconnected) run->request_stop();
    }
}

Result<void> Client::disconnect() {
    const auto impl = impl_;
    const auto run = impl->current();
    if (run && run->executor->is_current())
        return Error{ErrorCode::busy, 0, "use request_disconnect in client callback"};
    std::unique_lock<std::mutex> lock(impl->operation, std::try_to_lock);
    if (!lock) return Error{ErrorCode::busy, 0, "client connect/disconnect in progress"};
    const auto active = impl->current();
    Result<void> result;
    if (active) {
        auto expected = ClientState::connected;
        if (impl->state->compare_exchange_strong(expected, ClientState::disconnecting) &&
            active->session->state() == session::State::associated) {
            result = active->service->release();
            // 请求正在等待时不排队 RELEASE，直接关闭以结束它的等待。
            if (!result && (result.error().code == ErrorCode::busy ||
                            result.error().code == ErrorCode::closed ||
                            result.error().code == ErrorCode::not_associated))
                result = {};
        }
        active->finish();
        std::lock_guard<std::mutex> guard(impl->mutex);
        impl->run.reset();
    }
    return result;
}

ClientState Client::state() const noexcept { return impl_->state->load(); }
}  // namespace dlt698::app
