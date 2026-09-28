#include <common/messaging/zmq_change_channel.h>
#include <zmq.h>

#if ZMQ_VERSION_MAJOR < 4 || (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR < 3)
#error "ems_zmq_messaging requires libzmq 4.3 or newer"
#endif

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

namespace ems::messaging {
namespace {

bool valid_topic(const std::string& topic) {
    if (topic.empty() || topic.size() > 255) return false;
    for (unsigned char ch : topic) {
        if (ch < 33 || ch > 126) return false;
    }
    return true;
}

bool valid_config(const ZmqChangeConfig& config) {
    return !config.endpoint.empty() && config.max_queue_items > 0 && config.max_message_bytes > 0 &&
           config.max_message_bytes <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           config.high_water_mark > 0;
}

void close_socket(void* socket, void* context) {
    if (socket) zmq_close(socket);
    if (context) zmq_ctx_term(context);
}

}  // namespace

struct ZmqChangePublisher::Impl {
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::queue<std::string> queue;
    std::thread worker;
    ZmqChangeConfig config;
    ZmqChangeStats counters;
    bool running{false};
    bool stopping{false};
};

ZmqChangePublisher::ZmqChangePublisher() : impl_(std::make_unique<Impl>()) {}

ZmqChangePublisher::~ZmqChangePublisher() { stop(); }

bool ZmqChangePublisher::start(const ZmqChangeConfig& config) {
    if (!valid_config(config)) return false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->running || impl_->worker.joinable()) return false;
        impl_->config = config;
        impl_->stopping = false;
    }

    std::promise<bool> initialized;
    auto future = initialized.get_future();
    impl_->worker = std::thread([state = impl_.get(), signal = std::move(initialized)]() mutable {
        void* context = zmq_ctx_new();
        void* socket = context ? zmq_socket(context, ZMQ_PUB) : nullptr;
        int linger = 0;
        int high_water_mark = state->config.high_water_mark;
        bool ok =
            socket && zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger)) == 0 &&
            zmq_setsockopt(socket, ZMQ_SNDHWM, &high_water_mark, sizeof(high_water_mark)) == 0 &&
            zmq_bind(socket, state->config.endpoint.c_str()) == 0;
        if (!ok) {
            close_socket(socket, context);
            signal.set_value(false);
            return;
        }
        signal.set_value(true);

        while (true) {
            std::string frame;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->ready.wait(lock,
                                  [state] { return state->stopping || !state->queue.empty(); });
                if (state->stopping) break;
                frame = std::move(state->queue.front());
                state->queue.pop();
            }
            if (zmq_send(socket, frame.data(), frame.size(), ZMQ_DONTWAIT) < 0) {
                std::lock_guard<std::mutex> lock(state->mutex);
                ++state->counters.send_errors;
            }
        }
        close_socket(socket, context);
    });
    if (!future.get()) {
        impl_->worker.join();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->running = true;
    }
    return true;
}

bool ZmqChangePublisher::publish(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running || impl_->stopping || !valid_topic(topic) ||
        topic.size() + 1 > impl_->config.max_message_bytes ||
        payload.size() > impl_->config.max_message_bytes - topic.size() - 1 ||
        impl_->queue.size() >= impl_->config.max_queue_items) {
        ++impl_->counters.rejected;
        return false;
    }
    std::string frame = topic;
    frame.push_back('\0');
    frame.append(payload);
    impl_->queue.push(std::move(frame));
    ++impl_->counters.accepted;
    impl_->ready.notify_one();
    return true;
}

ZmqChangeStats ZmqChangePublisher::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->counters;
}

void ZmqChangePublisher::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->running = false;
        impl_->stopping = true;
        std::queue<std::string> empty;
        impl_->queue.swap(empty);
    }
    impl_->ready.notify_one();
    if (impl_->worker.joinable()) impl_->worker.join();
}

struct ZmqChangeSubscriber::Impl {
    std::atomic<bool> stopping{false};
    std::thread worker;
};

ZmqChangeSubscriber::ZmqChangeSubscriber() : impl_(std::make_unique<Impl>()) {}

ZmqChangeSubscriber::~ZmqChangeSubscriber() { stop(); }

bool ZmqChangeSubscriber::start(const ZmqChangeConfig& config,
                                const std::vector<std::string>& topics, Callback callback) {
    if (!valid_config(config) || topics.empty() || !callback || impl_->worker.joinable())
        return false;
    for (const auto& topic : topics)
        if (!valid_topic(topic)) return false;

    impl_->stopping = false;
    std::promise<bool> initialized;
    auto future = initialized.get_future();
    impl_->worker =
        std::thread([state = impl_.get(), config, topics, callback = std::move(callback),
                     signal = std::move(initialized)]() mutable {
            void* context = zmq_ctx_new();
            void* socket = context ? zmq_socket(context, ZMQ_SUB) : nullptr;
            int linger = 0;
            int timeout_ms = 100;
            int high_water_mark = config.high_water_mark;
            std::int64_t max_size = static_cast<std::int64_t>(config.max_message_bytes);
            bool ok = socket && zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger)) == 0 &&
                      zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms)) == 0 &&
                      zmq_setsockopt(socket, ZMQ_RCVHWM, &high_water_mark,
                                     sizeof(high_water_mark)) == 0 &&
                      zmq_setsockopt(socket, ZMQ_MAXMSGSIZE, &max_size, sizeof(max_size)) == 0;
            if (ok) {
                for (const auto& topic : topics) {
                    std::string filter = topic;
                    filter.push_back('\0');
                    if (zmq_setsockopt(socket, ZMQ_SUBSCRIBE, filter.data(), filter.size()) != 0) {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok) ok = zmq_connect(socket, config.endpoint.c_str()) == 0;
            if (!ok) {
                close_socket(socket, context);
                signal.set_value(false);
                return;
            }
            signal.set_value(true);

            while (!state->stopping) {
                zmq_msg_t message;
                if (zmq_msg_init(&message) != 0) break;
                const int received = zmq_msg_recv(&message, socket, 0);
                if (received >= 0) {
                    const auto* bytes = static_cast<const char*>(zmq_msg_data(&message));
                    const std::size_t size = zmq_msg_size(&message);
                    if (size <= config.max_message_bytes) {
                        const void* separator = std::memchr(bytes, 0, size);
                        if (separator) {
                            const auto* split = static_cast<const char*>(separator);
                            std::string topic(bytes, split);
                            try {
                                callback(topic, std::string(split + 1, bytes + size));
                            } catch (...) {
                                // 消费者回调失败只影响此条可丢弃提示，后续提示继续接收。
                            }
                        }
                    }
                } else if (zmq_errno() != EAGAIN && zmq_errno() != EINTR) {
                    zmq_msg_close(&message);
                    break;
                }
                zmq_msg_close(&message);
            }
            close_socket(socket, context);
        });
    if (!future.get()) {
        impl_->worker.join();
        return false;
    }
    return true;
}

void ZmqChangeSubscriber::stop() noexcept {
    impl_->stopping = true;
    if (impl_->worker.joinable()) impl_->worker.join();
}

}  // namespace ems::messaging
