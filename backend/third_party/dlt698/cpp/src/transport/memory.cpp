#include <algorithm>
#include <dlt698/transport/memory.hpp>
#include <mutex>

namespace dlt698::transport {
namespace {
template <class H, class R>
void deliver(H handler, R result) {
    try {
        if (handler) handler(std::move(result));
    } catch (...) { /* 隔离用户回调。 */
    }
}
}  // namespace

struct MemoryChannel::Impl {
    struct Budget {
        std::mutex mutex;
        std::size_t bytes[2]{};
        std::size_t count[2]{};
    };

    struct Write {
        std::shared_ptr<Budget> budget;
        unsigned side;
        Bytes bytes;
        WriteHandler handler;
        bool reserved = false;

        void release() {
            if (!reserved) return;
            std::lock_guard<std::mutex> lock(budget->mutex);
            budget->bytes[side] -= bytes.size();
            --budget->count[side];
            reserved = false;
        }

        ~Write() { release(); }
    };

    struct End {
        Bytes bytes;
        ReadHandler reader;
        bool reading = false;
        bool closed = false;
    };

    std::shared_ptr<IExecutor> executor;
    MemoryOptions options;
    End ends[2];
    std::shared_ptr<Budget> budget = std::make_shared<Budget>();

    ~Impl() {
        // 排队任务仅弱引用共享状态；端点均销毁时仍将已有读取交付为 closed。
        for (auto& end : ends)
            if (end.reading)
                executor->post([handler = std::move(end.reader)]() mutable {
                    deliver(std::move(handler),
                            Result<Bytes>{Error{ErrorCode::closed, 0, "memory destroyed"}});
                });
    }

    void pump(unsigned side) {
        auto& end = ends[side];
        if (!end.reading) return;
        // 对端关闭后仍先交付已经进入缓存的数据，再交付 EOF。
        if (!end.closed && !end.bytes.empty()) {
            const auto n = std::min(end.bytes.size(), options.read_chunk_bytes);
            Bytes bytes(end.bytes.begin(), end.bytes.begin() + static_cast<std::ptrdiff_t>(n));
            end.bytes.erase(end.bytes.begin(), end.bytes.begin() + static_cast<std::ptrdiff_t>(n));
            end.reading = false;
            auto handler = std::move(end.reader);
            deliver(std::move(handler), Result<Bytes>{std::move(bytes)});
        } else if (end.closed || ends[1 - side].closed) {
            end.reading = false;
            auto handler = std::move(end.reader);
            deliver(std::move(handler),
                    Result<Bytes>{Error{ErrorCode::closed, 0, "memory channel"}});
        }
    }
};

MemoryChannel::MemoryChannel(std::shared_ptr<Impl> impl, unsigned side)
    : impl_(std::move(impl)), side_(side) {}

MemoryChannel::~MemoryChannel() { close(); }

std::pair<std::shared_ptr<MemoryChannel>, std::shared_ptr<MemoryChannel>> MemoryChannel::pair(
    std::shared_ptr<IExecutor> executor, MemoryOptions options) {
    if (!executor || !options.read_chunk_bytes || !options.max_buffer_bytes ||
        !options.max_pending_writes)
        throw std::invalid_argument("memory channel options");
    auto impl = std::make_shared<Impl>();
    impl->executor = std::move(executor);
    impl->options = options;
    return {std::shared_ptr<MemoryChannel>(new MemoryChannel(impl, 0)),
            std::shared_ptr<MemoryChannel>(new MemoryChannel(impl, 1))};
}

void MemoryChannel::async_read(ReadHandler handler) {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, side = side_, handler = std::move(handler)]() mutable {
        const auto impl = weak.lock();
        if (!impl) {
            deliver(std::move(handler), Result<Bytes>{Error{ErrorCode::closed, 0, "memory read"}});
            return;
        }
        auto& end = impl->ends[side];
        if (end.reading) {
            deliver(std::move(handler), Result<Bytes>{Error{ErrorCode::busy, 0, "memory read"}});
            return;
        }
        end.reading = true;
        end.reader = std::move(handler);
        impl->pump(side);
    });
}

void MemoryChannel::async_write(Bytes bytes, WriteHandler handler) {
    auto item = std::make_shared<Impl::Write>();
    item->budget = impl_->budget;
    item->side = side_;
    item->handler = std::move(handler);
    // 在投递前保留预算，防止执行器未运行时，写任务无限持有调用方传入的数据。
    {
        std::lock_guard<std::mutex> lock(item->budget->mutex);
        if (bytes.size() <= impl_->options.max_buffer_bytes - item->budget->bytes[side_] &&
            item->budget->count[side_] < impl_->options.max_pending_writes) {
            item->budget->bytes[side_] += bytes.size();
            ++item->budget->count[side_];
            item->reserved = true;
            item->bytes = std::move(bytes);
        }
    }
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, side = side_, item = std::move(item)]() mutable {
        const auto impl = weak.lock();
        const bool accepted = item->reserved;
        item->release();
        if (!impl) {
            deliver(std::move(item->handler),
                    Result<void>{Error{ErrorCode::closed, 0, "memory write"}});
            return;
        }
        auto& target = impl->ends[1 - side];
        if (target.closed || impl->ends[side].closed) {
            deliver(std::move(item->handler),
                    Result<void>{Error{ErrorCode::closed, 0, "memory write"}});
        } else if (!accepted ||
                   item->bytes.size() > impl->options.max_buffer_bytes - target.bytes.size()) {
            deliver(std::move(item->handler),
                    Result<void>{Error{ErrorCode::resource_limit, 0, "memory buffer"}});
        } else {
            target.bytes.insert(target.bytes.end(), item->bytes.begin(), item->bytes.end());
            // 进入接收缓存后立即释放待写副本，再允许回调重入提交后续写入。
            item->bytes = {};
            deliver(std::move(item->handler), Result<void>{});
            impl->pump(1 - side);
        }
    });
}

void MemoryChannel::close() {
    const std::weak_ptr<Impl> weak = impl_;
    impl_->executor->post([weak, side = side_] {
        const auto impl = weak.lock();
        if (!impl) return;
        impl->ends[side].closed = true;
        impl->ends[side].bytes.clear();
        impl->pump(side);
        impl->pump(1 - side);
    });
}
}  // namespace dlt698::transport
