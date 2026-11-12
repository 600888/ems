#include <deque>
#include <dlt698/protocol/link/frame.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <mutex>

namespace dlt698::transport {
namespace {
template <class H, class R>
void deliver(H handler, R result) {
    try {
        if (handler) handler(std::move(result));
    } catch (...) {
    }
}

struct Budget {
    std::mutex mutex;
    std::size_t bytes = 0, count = 0;
    SerialLinkOptions options;

    bool acquire(std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex);
        if (count >= options.max_pending_writes || n > options.max_pending_write_bytes - bytes)
            return false;
        bytes += n;
        ++count;
        return true;
    }

    void release(std::size_t n) {
        std::lock_guard<std::mutex> lock(mutex);
        bytes -= n;
        --count;
    }
};

struct Write {
    Bytes bytes;
    IChannel::WriteHandler handler;
    std::shared_ptr<Budget> budget;
    std::size_t size = 0;
    bool reserved = false, done = false, draining = false;
    std::optional<Error> error;

    ~Write() {
        if (reserved) budget->release(size);
    }

    void complete(Result<void> result) {
        if (done) return;
        done = true;
        if (reserved) {
            reserved = false;
            budget->release(size);
        }
        deliver(std::move(handler), std::move(result));
    }
};
}  // namespace

struct SerialLinkChannel::Impl : std::enable_shared_from_this<Impl> {
    std::shared_ptr<IChannel> raw;
    std::shared_ptr<IExecutor> executor;
    SerialLinkOptions options;
    std::shared_ptr<Budget> budget;
    std::deque<std::shared_ptr<Write>> writes;
    std::shared_ptr<ITimer> timer;
    IExecutor::Clock::time_point earliest{};
    bool closed = false, busy = false, transmitting = false;

    ~Impl() {
        if (timer) timer->cancel();
        raw->close();
        for (auto& item : writes)
            executor->post(
                [item] { item->complete(Error{ErrorCode::closed, 0, "serial link destroyed"}); });
        if (transmitting && options.set_transmit)
            executor->post([set = options.set_transmit] {
                try {
                    (void)set(false);
                } catch (...) {
                }
            });
    }

    std::chrono::nanoseconds bit_time(std::uint64_t bits) const {
        return std::chrono::nanoseconds((bits * 1000000000ULL + options.baud_rate - 1) /
                                        options.baud_rate);
    }

    Result<void> direction(bool sending) {
        if (!options.set_transmit) return {};
        try {
            return options.set_transmit(sending);
        } catch (...) {
            return Error{ErrorCode::io_error, 0, "serial direction hook"};
        }
    }

    void shutdown(Error error) {
        if (closed) return;
        closed = true;
        if (timer) timer->cancel();
        raw->close();
        if (transmitting) {
            (void)direction(false);
            transmitting = false;
        }
        for (auto& item : writes) item->complete(error);
        writes.clear();
    }

    void defer(IExecutor::Clock::duration delay, IExecutor::Task task) {
        try {
            timer = executor->schedule(delay, std::move(task));
        } catch (...) {
            shutdown({ErrorCode::invalid_value, 0, "serial timer range"});
        }
    }

    void drained(const std::shared_ptr<Write>& item, Result<void> result) {
        if (closed || writes.empty() || writes.front() != item || !item->draining) return;
        item->draining = false;  // 忽略驱动重复完成，不重复切换方向或交付回调。
        if (!result) {
            shutdown(result.error());
            return;
        }
        const auto changed = direction(false);
        if (!changed) {
            shutdown(changed.error());
            return;
        }
        transmitting = false;
        // 最后停止位完成后才开始 33 位静默计时；不能把交给 OS 的时间当作物理排空。
        const std::weak_ptr<Impl> weak = shared_from_this();
        defer(bit_time(33), [weak, item] {
            const auto self = weak.lock();
            if (!self || self->closed) return;
            self->writes.pop_front();
            self->busy = false;
            item->complete({});
            self->pump();
        });
    }

    void written(const std::shared_ptr<Write>& item, Result<void> result) {
        if (closed) return;
        if (!result) {
            shutdown(result.error());
            return;
        }
        item->draining = true;
        const std::weak_ptr<Impl> weak = shared_from_this();
        if (options.async_drain) {
            try {
                options.async_drain([weak, item](Result<void> drained_result) {
                    if (const auto self = weak.lock())
                        self->executor->post([weak, item, result = std::move(drained_result)] {
                            if (const auto self = weak.lock()) self->drained(item, result);
                        });
                });
            } catch (...) {
                shutdown({ErrorCode::io_error, 0, "serial drain hook"});
            }
        } else {
            // 无硬件 drain 时只做保守软件估算，不宣称已验证 USB/RS-485 的物理时序。
            defer(bit_time(item->size * options.bits_per_character), [weak, item] {
                if (const auto self = weak.lock()) self->drained(item, {});
            });
        }
    }

    void pump() {
        if (closed || busy || writes.empty()) return;
        const std::weak_ptr<Impl> weak = shared_from_this();
        if (earliest > executor->now()) {
            if (timer) timer->cancel();
            defer(earliest - executor->now(), [weak] {
                if (const auto self = weak.lock()) self->pump();
            });
            return;
        }
        busy = true;
        transmitting = true;
        const auto changed = direction(true);
        if (!changed) {
            shutdown(changed.error());
            return;
        }
        auto item = writes.front();
        raw->async_write(std::move(item->bytes), [weak, item](Result<void> result) {
            if (const auto self = weak.lock())
                self->executor->post([weak, item, result = std::move(result)] {
                    if (const auto self = weak.lock()) self->written(item, result);
                });
        });
    }
};

SerialLinkChannel::SerialLinkChannel(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

SerialLinkChannel::~SerialLinkChannel() = default;

std::shared_ptr<SerialLinkChannel> SerialLinkChannel::wrap(std::shared_ptr<IChannel> raw,
                                                           std::shared_ptr<IExecutor> executor,
                                                           SerialLinkOptions options) {
    if (!raw || !executor || !options.baud_rate || options.bits_per_character < 7 ||
        options.bits_per_character > 12 || !options.max_pending_write_bytes ||
        options.max_pending_write_bytes > 1024 * 1024 || !options.max_pending_writes ||
        (options.set_transmit && !options.async_drain))
        throw std::invalid_argument("serial link options/drain");
    auto impl = std::make_shared<Impl>();
    impl->raw = std::move(raw);
    impl->executor = std::move(executor);
    impl->options = std::move(options);
    impl->budget = std::make_shared<Budget>();
    // 预算只保存尺寸限制，不能复制 hooks（它们可能拥有应用资源），避免预算延长驱动生命周期。
    impl->budget->options.max_pending_write_bytes = impl->options.max_pending_write_bytes;
    impl->budget->options.max_pending_writes = impl->options.max_pending_writes;
    return std::shared_ptr<SerialLinkChannel>(new SerialLinkChannel(std::move(impl)));
}

void SerialLinkChannel::async_read(ReadHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->raw->async_read([weak, handler = std::move(handler)](Result<Bytes> result) mutable {
        const auto self = weak.lock();
        if (!self) {
            deliver(std::move(handler),
                    Result<Bytes>{Error{ErrorCode::closed, 0, "serial link destroyed"}});
            return;
        }
        self->executor->post(
            [weak, handler = std::move(handler), result = std::move(result)]() mutable {
                const auto self = weak.lock();
                if (!self || self->closed) {
                    deliver(std::move(handler),
                            Result<Bytes>{Error{ErrorCode::closed, 0, "serial link closed"}});
                    return;
                }
                if (result) {
                    const auto now = self->executor->now();
                    const auto gap = self->bit_time(33);
                    self->earliest = gap <= IExecutor::Clock::time_point::max() - now
                                         ? now + gap
                                         : IExecutor::Clock::time_point::max();
                } else if (result.error().code != ErrorCode::busy)
                    self->shutdown(result.error());
                deliver(std::move(handler), std::move(result));
            });
    });
}

void SerialLinkChannel::async_write(Bytes bytes, WriteHandler handler) {
    auto item = std::make_shared<Write>();
    item->handler = std::move(handler);
    item->budget = impl_->budget;
    const auto frame = protocol::link::decode_frame(bytes);
    if (!frame)
        item->error = frame.error();
    else {
        item->size = bytes.size() + 4;
        item->reserved = item->budget->acquire(item->size);
        if (item->reserved) {
            bytes.insert(bytes.begin(), 4, 0xfe);
            item->bytes = std::move(bytes);
        } else
            item->error = Error{ErrorCode::resource_limit, 0, "serial link queue"};
    }
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, item] {
        const auto self = weak.lock();
        if (!self || self->closed) {
            item->complete(Error{ErrorCode::closed, 0, "serial link closed"});
            return;
        }
        if (item->error) {
            item->complete(*item->error);
            return;
        }
        self->writes.push_back(item);
        self->pump();
    });
}

void SerialLinkChannel::close() {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak] {
        if (const auto self = weak.lock())
            self->shutdown({ErrorCode::closed, 0, "serial link closed"});
    });
}
}  // namespace dlt698::transport
