#include <asio.hpp>
#include <atomic>
#include <deque>
#include <dlt698/transport/tcp.hpp>
#include <map>
#include <mutex>

#include "detail.hpp"

namespace dlt698::transport {
using detail::deliver;
using detail::io_error;
using detail::validate;
using detail::WriteBudget;

IoRuntime::IoRuntime() : impl_(std::make_shared<Impl>()) {}

IoRuntime::~IoRuntime() { stop(); }

void IoRuntime::run() { impl_->context.run(); }

void IoRuntime::run_for(std::chrono::milliseconds duration) { impl_->context.run_for(duration); }

void IoRuntime::stop() { impl_->context.stop(); }

void IoRuntime::finish() {
    const auto impl = impl_;
    // 将守卫释放排入事件循环；socket 取消回调、会话关闭及计时器取消仍属于在途工作。
    // 自然耗尽工作后退出，而不是 stop() 强行中断并遗留尚未交付的完成通知。
    asio::post(impl->context, [impl] {
        std::lock_guard<std::mutex> lock(impl->work_mutex);
        impl->work.reset();
    });
}

void IoRuntime::restart() { impl_->context.restart(); }

std::shared_ptr<IExecutor> IoRuntime::executor() {
    struct Executor final : IExecutor, std::enable_shared_from_this<Executor> {
        struct TimerState {
            asio::steady_timer timer;
            Task task;
            std::atomic<bool> cancelled{false};

            TimerState(asio::io_context& context, Task value)
                : timer(context), task(std::move(value)) {}
        };

        struct Token final : ITimer {
            std::weak_ptr<Executor> owner;
            std::weak_ptr<TimerState> state;

            void cancel() override {
                const auto executor = owner.lock();
                const auto timer = state.lock();
                if (!timer || !executor) return;
                timer->cancelled = true;
                const auto weak_owner = owner;
                const auto weak_state = state;
                executor->post([weak_owner, weak_state] {
                    const auto executor = weak_owner.lock();
                    const auto timer = weak_state.lock();
                    if (executor && timer) {
                        timer->timer.cancel();
                        std::lock_guard<std::mutex> lock(executor->mutex);
                        executor->timers.erase(timer.get());
                    }
                });
            }
        };

        std::shared_ptr<Impl> runtime;
        asio::strand<asio::io_context::executor_type> serial;
        std::mutex mutex;
        std::map<TimerState*, std::shared_ptr<TimerState>> timers;

        explicit Executor(std::shared_ptr<Impl> value)
            : runtime(std::move(value)), serial(asio::make_strand(runtime->context)) {}

        void post(Task task) override {
            asio::post(serial, [task = std::move(task)] {
                try {
                    if (task) task();
                } catch (...) { /* 隔离应用任务异常。 */
                }
            });
        }

        std::shared_ptr<ITimer> schedule(Clock::duration delay, Task task) override {
            if (delay < Clock::duration::zero() || delay > Clock::time_point::max() - now())
                throw std::invalid_argument("timer delay");
            const auto deadline = now() + delay;
            auto state = std::make_shared<TimerState>(runtime->context, std::move(task));
            {
                std::lock_guard<std::mutex> lock(mutex);
                timers.emplace(state.get(), state);
            }
            auto token = std::make_shared<Token>();
            token->owner = shared_from_this();
            token->state = state;
            const std::weak_ptr<Executor> weak_owner = shared_from_this();
            const std::weak_ptr<TimerState> weak_state = state;
            // 等待处理器仅持有弱引用，由执行器保活计时器，避免上下文和计时器互相保活。
            post([weak_owner, weak_state, deadline] {
                const auto owner = weak_owner.lock();
                const auto state = weak_state.lock();
                if (!owner || !state) return;
                if (state->cancelled) {
                    std::lock_guard<std::mutex> lock(owner->mutex);
                    owner->timers.erase(state.get());
                    return;
                }
                state->timer.expires_at(deadline);
                state->timer.async_wait(asio::bind_executor(
                    owner->serial, [weak_owner, weak_state](asio::error_code error) {
                        const auto owner = weak_owner.lock();
                        const auto state = weak_state.lock();
                        if (!owner || !state) return;
                        {
                            std::lock_guard<std::mutex> lock(owner->mutex);
                            owner->timers.erase(state.get());
                        }
                        if (!error && !state->cancelled) {
                            try {
                                if (state->task) state->task();
                            } catch (...) {
                            }
                        }
                    }));
            });
            return token;
        }

        Clock::time_point now() const noexcept override { return Clock::now(); }

        bool is_current() const noexcept override {
            return runtime->context.get_executor().running_in_this_thread();
        }
    };

    return std::make_shared<Executor>(impl_);
}

struct TcpChannel::Impl : std::enable_shared_from_this<TcpChannel::Impl> {
    struct Write {
        Bytes bytes;
        WriteHandler handler;
        std::shared_ptr<WriteBudget> budget;
        bool reserved = false;
        bool done = false;

        ~Write() {
            if (reserved) budget->release(bytes.size());
        }

        void complete(Result<void> result) {
            // 关闭队列和 socket 的异步完成可能先后到达，同一个写入只能通知一次。
            if (!done) {
                done = true;
                if (reserved) {
                    // 回调可能立即提交新写入，先释放预算再通知用户。
                    reserved = false;
                    budget->release(bytes.size());
                }
                deliver(handler, std::move(result));
            }
        }
    };

    std::shared_ptr<IoRuntime::Impl> runtime;
    // socket、读取状态和发送队列只在该 strand 中访问，允许外部线程并发投递操作。
    asio::strand<asio::io_context::executor_type> serial;
    asio::ip::tcp::socket socket;
    asio::ip::tcp::resolver resolver;
    ChannelOptions options;
    std::shared_ptr<WriteBudget> write_budget;
    bool closed = false;
    bool read_pending = false;
    std::deque<std::shared_ptr<Write>> writes;

    Impl(std::shared_ptr<IoRuntime::Impl> rt, ChannelOptions opt)
        : runtime(std::move(rt)),
          serial(asio::make_strand(runtime->context)),
          socket(runtime->context),
          resolver(runtime->context),
          options(opt),
          write_budget(std::make_shared<WriteBudget>(opt)) {}

    ~Impl() {
        // 内部挂起回调不持有 Impl 的强引用，销毁通道会释放 socket。
        // 避免形成 context -> 回调 -> 通道 -> context 的内部引用环。
        for (const auto& item : writes)
            asio::post(serial, [item] {
                item->complete(Error{ErrorCode::closed, 0, "channel destroyed"});
            });
    }

    void pump_write() {
        if (writes.empty() || closed) return;
        const std::weak_ptr<Impl> weak = shared_from_this();
        const auto item = writes.front();
        // async_write 确保整块发送完成后才推进队列；item 保持发送缓冲区存活。
        // 通道使用弱引用，使未完成 I/O 不会阻止应用销毁通道。
        asio::async_write(
            socket, asio::buffer(item->bytes),
            asio::bind_executor(serial, [weak, item](asio::error_code ec, std::size_t) {
                const auto self = weak.lock();
                if (!self) {
                    item->complete(Error{ErrorCode::closed, 0, "channel destroyed"});
                    return;
                }
                if (self->closed) {
                    item->complete(Error{ErrorCode::closed, 0, "channel closed"});
                    return;
                }
                self->writes.pop_front();
                item->complete(ec ? Result<void>{io_error(ec)} : Result<void>{});
                if (ec)
                    self->close_now();
                else
                    self->pump_write();
            }));
    }

    void close_now() {
        if (closed) return;
        closed = true;
        // 先标记关闭，再取消解析及 socket 操作；迟到回调据此返回 closed 而非继续发送。
        resolver.cancel();
        asio::error_code ignored;
        socket.cancel(ignored);
        socket.close(ignored);
        for (auto& item : writes) item->complete(Error{ErrorCode::closed, 0, "channel closed"});
        writes.clear();
    }
};

TcpChannel::TcpChannel(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

TcpChannel::~TcpChannel() = default;

std::shared_ptr<TcpChannel> TcpChannel::connect(std::shared_ptr<IoRuntime> runtime,
                                                std::string host, std::uint16_t port,
                                                ConnectHandler handler, ChannelOptions options) {
    if (!runtime) throw std::invalid_argument("null runtime");
    validate(options);
    auto impl = std::make_shared<Impl>(runtime->impl_, options);
    auto channel = std::shared_ptr<TcpChannel>(new TcpChannel(impl));
    const std::weak_ptr<Impl> weak = impl;
    // 连接从 strand 启动：先异步解析 DNS，再逐个尝试解析得到的 TCP 端点。
    // 即使对象在任一步销毁，完成回调仍能通过弱引用检查返回关闭错误。
    asio::post(impl->serial, [weak, host = std::move(host), port,
                              handler = std::move(handler)]() mutable {
        const auto impl = weak.lock();
        if (!impl) {
            deliver(handler, Result<void>{Error{ErrorCode::closed, 0, "channel destroyed"}});
            return;
        }
        if (impl->closed) {
            deliver(handler, Result<void>{Error{ErrorCode::closed, 0, "connect closed"}});
            return;
        }
        impl->resolver.async_resolve(
            host, std::to_string(port),
            asio::bind_executor(
                impl->serial,
                [weak, handler = std::move(handler)](
                    asio::error_code ec, asio::ip::tcp::resolver::results_type endpoints) mutable {
                    const auto impl = weak.lock();
                    if (!impl) {
                        deliver(handler,
                                Result<void>{Error{ErrorCode::closed, 0, "channel destroyed"}});
                        return;
                    }
                    if (impl->closed || ec) {
                        deliver(handler, Result<void>{impl->closed ? Error{ErrorCode::closed, 0,
                                                                           "connect closed"}
                                                                   : io_error(ec)});
                        return;
                    }
                    asio::async_connect(
                        impl->socket, endpoints,
                        asio::bind_executor(
                            impl->serial,
                            [weak, handler = std::move(handler)](
                                asio::error_code error, const asio::ip::tcp::endpoint&) mutable {
                                const auto impl = weak.lock();
                                if (!impl) {
                                    deliver(handler, Result<void>{Error{ErrorCode::closed, 0,
                                                                        "channel destroyed"}});
                                    return;
                                }
                                deliver(handler, impl->closed
                                                     ? Result<void>{Error{ErrorCode::closed, 0,
                                                                          "connect closed"}}
                                                 : error ? Result<void>{io_error(error)}
                                                         : Result<void>{});
                            }));
                }));
    });
    return channel;
}

void TcpChannel::async_read(ReadHandler handler) {
    const auto impl = impl_;
    const std::weak_ptr<Impl> weak = impl;
    asio::post(impl->serial, [weak, handler = std::move(handler)]() mutable {
        const auto impl = weak.lock();
        if (!impl) {
            deliver(handler, Result<Bytes>{Error{ErrorCode::closed, 0, "channel destroyed"}});
            return;
        }
        if (impl->closed || impl->read_pending) {
            deliver(handler, Result<Bytes>{Error{impl->closed ? ErrorCode::closed : ErrorCode::busy,
                                                 0, "channel read"}});
            return;
        }
        // 设置标记后才启动读取，避免同一通道出现两个同时消费字节流的操作。
        impl->read_pending = true;
        auto buffer = std::make_shared<Bytes>(impl->options.read_chunk_bytes);
        impl->socket.async_read_some(
            asio::buffer(*buffer),
            asio::bind_executor(impl->serial, [weak, buffer, handler = std::move(handler)](
                                                  asio::error_code ec, std::size_t count) mutable {
                const auto impl = weak.lock();
                if (!impl) {
                    deliver(handler,
                            Result<Bytes>{Error{ErrorCode::closed, 0, "channel destroyed"}});
                    return;
                }
                // 完成前清除标记，使用户能在回调里继续发起下一次读取。
                impl->read_pending = false;
                if (impl->closed)
                    deliver(handler, Result<Bytes>{Error{ErrorCode::closed, 0, "channel closed"}});
                else if (ec) {
                    deliver(handler, Result<Bytes>{io_error(ec)});
                    impl->close_now();
                } else {
                    buffer->resize(count);
                    deliver(handler, Result<Bytes>{std::move(*buffer)});
                }
            }));
    });
}

void TcpChannel::async_write(Bytes bytes, WriteHandler handler) {
    const auto impl = impl_;
    const std::weak_ptr<Impl> weak = impl;
    auto item = std::make_shared<Impl::Write>();
    item->bytes = std::move(bytes);
    item->handler = std::move(handler);
    item->budget = impl->write_budget;
    // 在 post 之前预占预算，防止运行时尚未驱动时大量投递任务持有无界缓冲区。
    item->reserved = item->budget->acquire(item->bytes.size());
    if (!item->reserved)
        Bytes{}.swap(item->bytes);  // 先释放被拒绝的缓冲区，再投递资源超限的完成通知。
    asio::post(impl->serial, [weak, item]() mutable {
        const auto impl = weak.lock();
        if (!impl || impl->closed) {
            item->complete(Error{ErrorCode::closed, 0, "channel write"});
            return;
        }
        if (!item->reserved) {
            item->complete(Error{ErrorCode::resource_limit, 0, "write queue"});
            return;
        }
        const bool idle = impl->writes.empty();
        impl->writes.push_back(item);
        // 仅空闲队列启动写操作，后续项由前一项的完成回调推进。
        if (idle) impl->pump_write();
    });
}

void TcpChannel::close() {
    const std::weak_ptr<Impl> weak = impl_;
    asio::post(impl_->serial, [weak] {
        if (const auto impl = weak.lock()) impl->close_now();
    });
}

struct TcpListener::Impl {
    std::shared_ptr<IoRuntime::Impl> runtime;
    asio::strand<asio::io_context::executor_type> serial;
    asio::ip::tcp::acceptor acceptor;
    ChannelOptions options;
    std::uint16_t port = 0;
    bool closed = false;
    bool pending = false;
    std::shared_ptr<TcpChannel::Impl> accepting_channel;

    Impl(std::shared_ptr<IoRuntime> rt, ChannelOptions opt)
        : runtime(rt->impl_),
          serial(asio::make_strand(runtime->context)),
          acceptor(runtime->context),
          options(opt) {}
};

TcpListener::TcpListener(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

TcpListener::~TcpListener() = default;

Result<std::shared_ptr<TcpListener>> TcpListener::listen(std::shared_ptr<IoRuntime> runtime,
                                                         const std::string& address,
                                                         std::uint16_t port,
                                                         ChannelOptions options) {
    return listen(std::move(runtime), address, port, options, false);
}

Result<std::shared_ptr<TcpListener>> TcpListener::listen(std::shared_ptr<IoRuntime> runtime,
                                                         const std::string& address,
                                                         std::uint16_t port, ChannelOptions options,
                                                         bool exclusive_address) {
    if (!runtime) return Error{ErrorCode::invalid_value, 0, "null runtime"};
    validate(options);
    auto impl = std::make_shared<Impl>(std::move(runtime), options);
    asio::error_code ec;
    auto ip = asio::ip::make_address(address, ec);
    if (ec) return io_error(ec);
    const asio::ip::tcp::endpoint endpoint(ip, port);
    // 监听建立为同步步骤，逐项返回系统错误；端口为零时在绑定后保存实际分配端口。
    impl->acceptor.open(endpoint.protocol(), ec);
    if (ec) return io_error(ec);
#ifdef _WIN32
    if (exclusive_address) {
        // Windows 的 SO_REUSEADDR 允许抢占已有监听端口；高层服务器要求启动时可靠报冲突。
        const int exclusive = 1;
        if (::setsockopt(impl->acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                         reinterpret_cast<const char*>(&exclusive),
                         static_cast<int>(sizeof(exclusive))) == SOCKET_ERROR)
            ec = asio::error_code(::WSAGetLastError(), asio::error::get_system_category());
    } else
        impl->acceptor.set_option(asio::socket_base::reuse_address(true), ec);
#else
    (void)exclusive_address;
    impl->acceptor.set_option(asio::socket_base::reuse_address(true), ec);
#endif
    if (ec) return io_error(ec);
    impl->acceptor.bind(endpoint, ec);
    if (ec) return io_error(ec);
    impl->acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) return io_error(ec);
    impl->port = impl->acceptor.local_endpoint(ec).port();
    if (ec) return io_error(ec);
    return std::shared_ptr<TcpListener>(new TcpListener(std::move(impl)));
}

std::uint16_t TcpListener::local_port() const noexcept { return impl_->port; }

void TcpListener::async_accept(AcceptHandler handler) {
    const auto impl = impl_;
    const std::weak_ptr<Impl> weak = impl;
    asio::post(impl->serial, [weak, handler = std::move(handler)]() mutable {
        const auto impl = weak.lock();
        if (!impl) {
            deliver(handler, Result<std::shared_ptr<TcpChannel>>{
                                 Error{ErrorCode::closed, 0, "listener destroyed"}});
            return;
        }
        if (impl->closed || impl->pending) {
            deliver(handler, Result<std::shared_ptr<TcpChannel>>{Error{
                                 impl->closed ? ErrorCode::closed : ErrorCode::busy, 0, "accept"}});
            return;
        }
        impl->pending = true;
        auto channel_impl = std::make_shared<TcpChannel::Impl>(impl->runtime, impl->options);
        // 接受期间由监听器保活待连接 socket，完成回调只持有弱引用，避免内部引用环。
        impl->accepting_channel = channel_impl;
        const std::weak_ptr<TcpChannel::Impl> weak_channel = channel_impl;
        impl->acceptor.async_accept(
            channel_impl->socket,
            asio::bind_executor(impl->serial, [weak, weak_channel, handler = std::move(handler)](
                                                  asio::error_code ec) mutable {
                const auto impl = weak.lock();
                const auto channel_impl = weak_channel.lock();
                if (!impl) {
                    deliver(handler, Result<std::shared_ptr<TcpChannel>>{
                                         Error{ErrorCode::closed, 0, "listener destroyed"}});
                    return;
                }
                impl->pending = false;
                // 在通知用户之前清理接受状态，允许回调直接发起下一次接受操作。
                impl->accepting_channel.reset();
                if (ec || impl->closed) {
                    deliver(handler, Result<std::shared_ptr<TcpChannel>>{
                                         impl->closed ? Error{ErrorCode::closed, 0, "accept closed"}
                                                      : io_error(ec)});
                } else
                    deliver(handler,
                            Result<std::shared_ptr<TcpChannel>>{
                                std::shared_ptr<TcpChannel>(new TcpChannel(channel_impl))});
            }));
    });
}

void TcpListener::close() {
    const std::weak_ptr<Impl> weak = impl_;
    asio::post(impl_->serial, [weak] {
        const auto impl = weak.lock();
        if (!impl) return;
        if (impl->closed) return;
        impl->closed = true;
        asio::error_code ignored;
        impl->acceptor.cancel(ignored);
        impl->acceptor.close(ignored);
    });
}

}  // namespace dlt698::transport
