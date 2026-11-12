#include <condition_variable>
#include <dlt698/service/sync.hpp>
#include <mutex>

namespace dlt698::service {
struct SyncClientService::Impl {
    std::shared_ptr<session::Session> session;
    ClientService client;
    Drive drive;
    std::mutex operation;

    Impl(std::shared_ptr<session::Session> value, Drive pump)
        : session(std::move(value)), client(session), drive(std::move(pump)) {}

    template <class T, class Start>
    Result<T> wait(Start start) {
        // 先检查整个执行环境，连同其他 strand 的回调一起拒绝，避免单运行线程自锁。
        if (session->in_executor_thread())
            return Error{ErrorCode::busy, 0, "sync call in executor"};
        std::unique_lock<std::mutex> serial(operation, std::try_to_lock);
        if (!serial.owns_lock()) return Error{ErrorCode::busy, 0, "sync operation in progress"};

        struct Completion {
            std::mutex mutex;
            std::condition_variable changed;
            std::optional<Result<T>> result;
        };

        auto completion = std::make_shared<Completion>();
        // 完成状态由回调拥有，不借用栈上 promise；驱动异常返回后迟到回调也安全。
        start([completion](Result<T> result) {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (!completion->result) completion->result = std::move(result);
            completion->changed.notify_all();
        });
        std::unique_lock<std::mutex> lock(completion->mutex);
        while (!completion->result) {
            if (!drive)
                completion->changed.wait(lock, [&] { return bool(completion->result); });
            else {
                lock.unlock();
                try {
                    drive(std::chrono::milliseconds(10));
                } catch (...) {
                    session->cancel();
                    return Error{ErrorCode::io_error, 0, "sync driver failed"};
                }
                lock.lock();
            }
        }
        return std::move(*completion->result);
    }
};

SyncClientService::SyncClientService(std::shared_ptr<session::Session> session, Drive drive)
    : impl_(std::make_unique<Impl>(std::move(session), std::move(drive))) {}

SyncClientService::~SyncClientService() = default;

Result<protocol::apdu::ConnectResponse> SyncClientService::connect() {
    return impl_->wait<protocol::apdu::ConnectResponse>(
        [&](auto handler) { impl_->session->async_connect(std::move(handler)); });
}

Result<ObjectValue> SyncClientService::get(model::Oad attribute) {
    return impl_->wait<ObjectValue>(
        [&](auto handler) { impl_->client.async_get(attribute, std::move(handler)); });
}

Result<protocol::apdu::GetResponse> SyncClientService::get_list(
    std::vector<model::Oad> attributes) {
    return impl_->wait<protocol::apdu::GetResponse>([&](auto handler) {
        impl_->client.async_get_list(std::move(attributes), std::move(handler));
    });
}

Result<protocol::apdu::RecordResult> SyncClientService::get_record(
    protocol::apdu::GetRecord record) {
    return impl_->wait<protocol::apdu::RecordResult>(
        [&](auto h) { impl_->client.async_get_record(std::move(record), std::move(h)); });
}

Result<protocol::apdu::GetRecordResponse> SyncClientService::get_record_list(
    std::vector<protocol::apdu::GetRecord> records) {
    return impl_->wait<protocol::apdu::GetRecordResponse>(
        [&](auto h) { impl_->client.async_get_record_list(std::move(records), std::move(h)); });
}

Result<std::uint8_t> SyncClientService::set(model::Oad attribute, model::Data value) {
    return impl_->wait<std::uint8_t>([&](auto handler) {
        impl_->client.async_set(attribute, std::move(value), std::move(handler));
    });
}

Result<protocol::apdu::SetResponse> SyncClientService::set_list(
    std::vector<protocol::apdu::SetAttribute> attributes) {
    return impl_->wait<protocol::apdu::SetResponse>([&](auto handler) {
        impl_->client.async_set_list(std::move(attributes), std::move(handler));
    });
}

Result<ActionValue> SyncClientService::action(model::Omd method, model::Data parameter) {
    return impl_->wait<ActionValue>([&](auto handler) {
        impl_->client.async_action(method, std::move(parameter), std::move(handler));
    });
}

Result<protocol::apdu::ActionResponse> SyncClientService::action_list(
    std::vector<protocol::apdu::ActionMethod> methods) {
    return impl_->wait<protocol::apdu::ActionResponse>([&](auto handler) {
        impl_->client.async_action_list(std::move(methods), std::move(handler));
    });
}

Result<void> SyncClientService::release() {
    return impl_->wait<void>(
        [&](auto handler) { impl_->session->async_release(std::move(handler)); });
}
}  // namespace dlt698::service
