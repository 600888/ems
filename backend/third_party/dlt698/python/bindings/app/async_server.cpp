#include "async_server.hpp"

namespace dlt698::python {
ServerRunner::ServerRunner(std::shared_ptr<app::Server> server) : server_(std::move(server)) {
    if (!server_) throw std::invalid_argument("ServerRunner requires a server");
}

ServerRunner::~ServerRunner() {
    // 停止请求可能早于正在执行的启动；先回收管理线程，再停止迟到的运行。
    server_->request_stop();
    if (worker_.joinable()) worker_.join();
    (void)server_->stop();
}

void ServerRunner::check() const {
    if (owner_ != std::this_thread::get_id())
        throw std::runtime_error("ServerRunner requires its owner thread");
}

std::uint64_t ServerRunner::submit(std::function<Result<void>()> operation) {
    check();
    if (worker_.joinable()) throw std::runtime_error("ServerRunner operation is pending");
    const auto token = next_token_++;
    finished_.store(false, std::memory_order_relaxed);
    worker_ = std::thread([this, token, operation = std::move(operation)] {
        ServerOperation done;
        done.token = token;
        try {
            auto result = operation();
            if (!result) done.error = result.error();
        } catch (const std::exception& error) {
            done.error = Error{ErrorCode::invalid_value, 0, error.what()};
        } catch (...) {
            done.error = Error{ErrorCode::invalid_value, 0, "server operation failed"};
        }
        completion_ = std::move(done);
        // release/acquire 发布完整结果；Python 不必加锁，也不会等待正在运行的操作。
        finished_.store(true, std::memory_order_release);
    });
    return token;
}

std::uint64_t ServerRunner::start_tcp(std::string address, std::uint16_t port,
                                      app::ConnectionProfile profile) {
    return submit([server = server_, address = std::move(address), port, profile] {
        return server->start_tcp(address, port, profile);
    });
}

std::uint64_t ServerRunner::start_serial(std::string path, unsigned baud,
                                         app::ConnectionProfile profile) {
    return submit([server = server_, path = std::move(path), baud, profile] {
        return server->start_serial(path, baud, profile);
    });
}

std::uint64_t ServerRunner::start_serial_configured(std::string path,
                                                    transport::SerialOptions serial,
                                                    transport::SerialLinkOptions link,
                                                    app::ConnectionProfile profile) {
    if (link.set_transmit || link.async_drain)
        throw std::invalid_argument(
            "Python serial hooks require a caller-driven Engine or SerialLinkChannel");
    return submit([server = server_, path = std::move(path), serial, link, profile] {
        return server->start_serial(path, serial, link, profile);
    });
}

std::uint64_t ServerRunner::stop() {
    return submit([server = server_] { return server->stop(); });
}

std::optional<ServerOperation> ServerRunner::poll() {
    check();
    if (!worker_.joinable() || !finished_.load(std::memory_order_acquire)) return std::nullopt;
    // 只有完成标志发布后才 join；线程已完成原生调用，最多等待返回的尾部指令。
    worker_.join();
    auto result = std::move(completion_);
    completion_.reset();
    return result;
}
}  // namespace dlt698::python
