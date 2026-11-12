#include "trans.hpp"

namespace dlt698::python {
TransBridge::TransBridge(std::size_t max_pending, std::size_t max_bytes)
    : max_pending_(max_pending), max_bytes_(max_bytes) {
    if (!max_pending || !max_bytes)
        throw std::invalid_argument("transparent bridge budget is zero");
}

TransBridge::~TransBridge() { close(); }

service::IProxyProvider::Cancel TransBridge::submit(
    protocol::apdu::ProxyTransRequest request,
    std::function<void(Result<protocol::apdu::ProxyTransResponse>)> handler) {
    std::optional<Error> error;
    std::uint64_t token = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
            error = Error{ErrorCode::closed, 0, "transparent bridge closed"};
        else if (pending_.size() >= max_pending_ || request.command.size() > max_bytes_ - bytes_)
            error = Error{ErrorCode::resource_limit, 0, "transparent bridge budget"};
        else {
            bytes_ += request.command.size();
            token = next_++;
            pending_.emplace(token, Pending{std::move(request), std::move(handler)});
        }
    }
    // 原生完成可以投递同一桥或运行时，必须在锁外调用以避免重入死锁。
    if (error) {
        handler(*error);
        return {};
    }
    std::weak_ptr<TransBridge> weak = shared_from_this();
    return [weak, token] {
        if (auto bridge = weak.lock()) bridge->cancel(token);
    };
}

std::vector<TransJob> TransBridge::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TransJob> output;
    for (auto& item : pending_) {
        if (item.second.dispatched) continue;
        output.push_back({item.first, item.second.request});
        item.second.dispatched = true;
    }
    return output;
}

bool TransBridge::complete(std::uint64_t token, Result<protocol::apdu::ProxyTransResponse> result) {
    std::function<void(Result<protocol::apdu::ProxyTransResponse>)> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pending_.find(token);
        if (it == pending_.end()) return false;
        if (result && !(result.value().port == it->second.request.port))
            throw std::invalid_argument("transparent response port differs");
        bytes_ -= it->second.request.command.size();
        handler = std::move(it->second.handler);
        pending_.erase(it);
    }
    handler(std::move(result));
    return true;
}

void TransBridge::cancel(std::uint64_t token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(token);
    if (it == pending_.end()) return;
    bytes_ -= it->second.request.command.size();
    pending_.erase(it);
}

void TransBridge::close() {
    std::map<std::uint64_t, Pending> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        closed_ = true;
        bytes_ = 0;
        pending.swap(pending_);
    }
    for (auto& item : pending)
        item.second.handler(Error{ErrorCode::closed, 0, "transparent bridge closed"});
}
}  // namespace dlt698::python
