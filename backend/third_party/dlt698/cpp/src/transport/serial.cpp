#include <deque>
#include <dlt698/transport/serial.hpp>

#include "detail.hpp"

namespace dlt698::transport {
using detail::deliver;
using detail::io_error;
using detail::validate;
using detail::WriteBudget;

struct SerialChannel::Impl : std::enable_shared_from_this<SerialChannel::Impl> {
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
    asio::serial_port socket;
    ChannelOptions options;
    std::shared_ptr<WriteBudget> write_budget;
    bool closed = false;
    bool read_pending = false;
    std::deque<std::shared_ptr<Write>> writes;

    Impl(std::shared_ptr<IoRuntime::Impl> rt, ChannelOptions opt)
        : runtime(std::move(rt)),
          serial(asio::make_strand(runtime->context)),
          socket(runtime->context),
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
        // 先标记关闭，再取消端口操作；迟到回调据此返回 closed 而非继续发送。
        asio::error_code ignored;
        socket.cancel(ignored);
        socket.close(ignored);
        for (auto& item : writes) item->complete(Error{ErrorCode::closed, 0, "channel closed"});
        writes.clear();
    }
};

SerialChannel::SerialChannel(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

SerialChannel::~SerialChannel() = default;

Result<std::shared_ptr<SerialChannel>> SerialChannel::open(std::shared_ptr<IoRuntime> runtime,
                                                           const std::string& device,
                                                           SerialOptions options) {
    if (!runtime || device.empty() || !options.baud_rate || options.data_bits < 5 ||
        options.data_bits > 8 || static_cast<unsigned>(options.parity) > 2 ||
        static_cast<unsigned>(options.stop_bits) > 2 ||
        static_cast<unsigned>(options.flow_control) > 2)
        return Error{ErrorCode::invalid_value, 0, "serial options"};
    try {
        validate(options.channel);
    } catch (const std::invalid_argument&) {
        return Error{ErrorCode::invalid_value, 0, "serial channel options"};
    }
    auto impl = std::make_shared<Impl>(runtime->impl_, options.channel);
    asio::error_code ec;
    auto path = device;
#ifdef _WIN32
    // 使用设备命名空间兼容 COM10 以上端口；已带 \\.\ 前缀的路径保持原样。
    if (device.size() > 3 && (device[0] == 'C' || device[0] == 'c') &&
        (device[1] == 'O' || device[1] == 'o') && (device[2] == 'M' || device[2] == 'm') &&
        device.find_first_not_of("0123456789", 3) == std::string::npos)
        path = "\\\\.\\" + device;
#endif
    impl->socket.open(path, ec);
    if (ec) return io_error(ec);
    using Base = asio::serial_port_base;
    impl->socket.set_option(Base::baud_rate(options.baud_rate), ec);
    if (!ec) impl->socket.set_option(Base::character_size(options.data_bits), ec);
    const Base::parity::type parities[] = {Base::parity::none, Base::parity::odd,
                                           Base::parity::even};
    const Base::stop_bits::type stops[] = {Base::stop_bits::one, Base::stop_bits::onepointfive,
                                           Base::stop_bits::two};
    const Base::flow_control::type flows[] = {
        Base::flow_control::none, Base::flow_control::software, Base::flow_control::hardware};
    if (!ec)
        impl->socket.set_option(Base::parity(parities[static_cast<unsigned>(options.parity)]), ec);
    if (!ec)
        impl->socket.set_option(Base::stop_bits(stops[static_cast<unsigned>(options.stop_bits)]),
                                ec);
    if (!ec)
        impl->socket.set_option(
            Base::flow_control(flows[static_cast<unsigned>(options.flow_control)]), ec);
    // 配置失败时不泄漏已打开设备；端口与运行时生命周期仍由弱引用完成路径管理。
    if (ec) return io_error(ec);
    return std::shared_ptr<SerialChannel>(new SerialChannel(std::move(impl)));
}

void SerialChannel::async_read(ReadHandler handler) {
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

void SerialChannel::async_write(Bytes bytes, WriteHandler handler) {
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

void SerialChannel::close() {
    const std::weak_ptr<Impl> weak = impl_;
    asio::post(impl_->serial, [weak] {
        if (const auto impl = weak.lock()) impl->close_now();
    });
}

}  // namespace dlt698::transport
