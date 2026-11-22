#include <asio.hpp>
#include "dlt645/common/log.h"
#include "dlt645/common/transform.h"
#include "dlt645/transport/client/client_api.h"
#include "transport/read_frame.h"
#include "log/default_logger.hpp"

namespace dlt645 {
    namespace transport {
        namespace client {

            struct TcpClient::Io {
                asio::io_context context;
                std::shared_ptr<asio::ip::tcp::socket> socket;
                asio::executor_work_guard<asio::io_context::executor_type> workGuard { asio::make_work_guard(context) };
                std::shared_ptr<const TrafficHandler> traffic;
            };

            TcpClient::TcpClient()
                : io_(std::make_unique<Io>())
                , isConnected_(false)
            {
            }

            TcpClient::~TcpClient()
            {
                if (io_thread_.joinable()) {
                    try {
                        disconnect();
                    } catch (...) {
                        io_->context.stop();
                    }
                    io_->workGuard.reset();
                    io_thread_.join();
                }
            }

            bool TcpClient::configure(const TcpClientConfig& config)
            {
                config_ = config;
                io_->traffic = config_.traffic ? std::make_shared<const TrafficHandler>(std::move(config_.traffic)) : nullptr;
                return true;
            }

            void TcpClient::ensureIoContextRunning()
            {
                if (!io_thread_.joinable()) {
                    io_thread_ = std::thread([this]() {
                        try {
                            io_->context.run();
                        } catch (const std::exception& e) {
                            LOG_ERROR("IO Context thread exception: {}", e.what());
                        }
                    });
                }
            }

            std::future<bool> TcpClient::connectAsync()
            {
                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                try {
                    ensureIoContextRunning();

                    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(config_.ip), config_.port);
                    auto socket = std::make_shared<asio::ip::tcp::socket>(io_->context);
                    io_->socket = socket;

                    LOG_INFO(
                        "Attempting to connect to {}:{} with timeout {} ms", config_.ip, config_.port, config_.timeout.count());

                    // 使用steady_timer实现连接超时
                    auto timer = std::make_shared<asio::steady_timer>(io_->context);
                    timer->expires_after(config_.timeout);

                    // 创建一个标志来跟踪哪个操作先完成
                    auto completed = std::make_shared<std::atomic<bool>>(false);

                    // 设置定时器回调
                    timer->async_wait([this, promise, completed, socket](const std::error_code& ec) {
                        if (!completed->exchange(true)) { // 如果是第一个完成的操作
                            if (ec != asio::error::operation_aborted) {
                                // 超时，取消连接操作
                                std::error_code ignored;
                                socket->cancel(ignored);
                                LOG_WARN("TCP connection timeout after {} ms", config_.timeout.count());
                                isConnected_ = false;
                                promise->set_value(false);
                            }
                        }
                    });

                    socket->async_connect(endpoint, [this, promise, timer, completed, socket](const std::error_code& error) {
                        // 取消定时器
                        timer->cancel();

                        if (!completed->exchange(true)) { // 如果是第一个完成的操作
                            if (!error) {
                                isConnected_ = true;
                                LOG_INFO("TCP client connected to {}:{} successfully", config_.ip, config_.port);
                                promise->set_value(true);
                            } else {
                                LOG_ERROR("TCP connection to {}:{} failed: {}", config_.ip, config_.port, error.message());
                                LOG_DEBUG(
                                    "Possible reasons: server not running, firewall blocking, incorrect IP/port, network issues");
                                isConnected_ = false;
                                promise->set_value(false);
                            }
                        }
                    });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in TCP connectAsync: {}", e.what());
                    promise->set_value(false);
                }

                return future;
            }

            std::future<void> TcpClient::disconnectAsync()
            {
                auto promise = std::make_shared<std::promise<void>>();
                auto future = promise->get_future();
                if (!io_thread_.joinable()) {
                    promise->set_value();
                    return future;
                }

                try {
                    asio::post(io_->context, [this, promise]() {
                        try {
                            if (io_->socket && io_->socket->is_open()) {
                                std::error_code ec;
                                io_->socket->close(ec);
                                if (ec) {
                                    LOG_WARN("Error closing TCP socket: {}", ec.message());
                                }
                            }
                            decoder_.clear();
                            isConnected_ = false;
                            LOG_INFO("TCP client disconnected");
                            promise->set_value();
                        } catch (const std::exception& e) {
                            LOG_ERROR("Exception in TCP disconnectAsync: {}", e.what());
                            promise->set_exception(std::current_exception());
                        }
                    });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in TCP disconnectAsync wrapper: {}", e.what());
                    promise->set_exception(std::current_exception());
                }

                return future;
            }

            std::future<std::vector<uint8_t>> TcpClient::sendRequestAsync(const std::vector<uint8_t>& frame)
            {
                LOG_INFO("TX: {}({})", dlt645::common::bytesToHexString(frame), frame.size());
                auto promise = std::make_shared<std::promise<std::vector<uint8_t>>>();
                auto future = promise->get_future();

                try {
                    if (!isConnected_ || !io_->socket || !io_->socket->is_open()) {
                        LOG_ERROR("TCP client not connected");
                        promise->set_value({});
                        return future;
                    }

                    auto buffer = std::make_shared<std::vector<uint8_t>>(frame);
                    auto socket = io_->socket;
                    auto traffic = io_->traffic;
                    const auto timeout = config_.timeout;
                    asio::async_write(
                        *socket,
                        asio::buffer(*buffer),
                        [this, promise, buffer, socket, traffic, timeout](const std::error_code& error, std::size_t) {
                            if (traffic) {
                                observeTraffic(*traffic, TrafficDirection::send,
                                               { buffer->data(), buffer->size() }, error);
                            }
                            if (error) {
                                LOG_ERROR("TCP send failed: {}", error.message());
                                isConnected_ = false;
                                promise->set_value({});
                                return;
                            }

                            std::make_shared<transport::FrameReadOperation<asio::ip::tcp::socket>>(
                                socket, decoder_, io_->context, timeout, promise, traffic)
                                ->start();
                        });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in TCP sendRequestAsync: {}", e.what());
                    promise->set_value({});
                }

                return future;
            }

            std::future<bool> TcpClient::sendOnlyAsync(const std::vector<uint8_t>& frame)
            {
                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();
                if (!isConnected_ || !io_->socket || !io_->socket->is_open()) {
                    promise->set_value(false);
                    return future;
                }
                auto buffer = std::make_shared<std::vector<uint8_t>>(frame);
                auto socket = io_->socket;
                auto traffic = io_->traffic;
                asio::async_write(*socket, asio::buffer(*buffer),
                                  [this, promise, buffer, socket, traffic](const std::error_code& error, size_t) {
                                      if (traffic) {
                                          observeTraffic(*traffic, TrafficDirection::send,
                                                         { buffer->data(), buffer->size() }, error);
                                      }
                                      if (error)
                                          isConnected_ = false;
                                      promise->set_value(!error);
                                  });
                return future;
            }

            bool TcpClient::isConnected() const { return isConnected_; }

            void TcpClient::setTimeout(std::chrono::milliseconds timeout) { config_.timeout = timeout; }

        } // namespace client
    } // namespace transport
} // namespace dlt645
