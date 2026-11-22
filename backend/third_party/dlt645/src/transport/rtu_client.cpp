#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <asio.hpp>
#include <system_error>
#include "dlt645/common/log.h"
#include "dlt645/transport/client/client_api.h"
#include "transport/read_frame.h"
#include "dlt645/common/transform.h"

namespace dlt645 {
    namespace transport {
        namespace client {

            struct RtuClient::Io {
                asio::io_context context;
                std::shared_ptr<asio::serial_port> serialPort;
                asio::executor_work_guard<asio::io_context::executor_type> workGuard { asio::make_work_guard(context) };
                std::shared_ptr<const TrafficHandler> traffic;
            };

            RtuClient::RtuClient()
                : io_(std::make_unique<Io>())
                , isConnected_(false)
            {
            }

            RtuClient::~RtuClient()
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

            bool RtuClient::configure(const RtuClientConfig& config)
            {
                config_ = config;
                io_->traffic = config_.traffic ? std::make_shared<const TrafficHandler>(std::move(config_.traffic)) : nullptr;
                return true;
            }

            void RtuClient::ensureIoContextRunning()
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

            bool RtuClient::configureSerialPort()
            {
                auto& port = *io_->serialPort;
                const auto& config = config_;
                try {
                    // 设置波特率
                    port.set_option(asio::serial_port_base::baud_rate(config.baudRate));

                    // 设置数据位
                    port.set_option(asio::serial_port_base::character_size(config.dataBits));

                    // 设置停止位
                    if (config.stopBits == 1) {
                        port.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
                    } else if (config.stopBits == 2) {
                        port.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::two));
                    } else {
                        port.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
                        LOG_WARN("Invalid stop bits value, using 1 stop bit");
                    }

                    // 设置校验位
                    if (config.parity == "even") {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::even));
                    } else if (config.parity == "odd") {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::odd));
                    } else {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
                    }

                    // 设置流控制
                    if (config.flowControl == 1) {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::software));
                    } else if (config.flowControl == 2) {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::hardware));
                    } else {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
                    }

                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to configure serial port: {}", e.what());
                    return false;
                }
            }

            std::future<bool> RtuClient::connectAsync()
            {
                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                try {
                    ensureIoContextRunning();

                    io_->serialPort = std::make_shared<asio::serial_port>(io_->context);

                    // 异步打开串口
                    asio::post(io_->context, [this, promise]() {
                        try {
                            io_->serialPort->open(config_.port);

                            // 配置串口参数
                            if (configureSerialPort()) {
                                isConnected_ = true;
                                LOG_INFO("RTU client connected to port {}", config_.port);
                                promise->set_value(true);
                            } else {
                                LOG_ERROR("Failed to configure serial port: {}", config_.port);
                                io_->serialPort->close();
                                isConnected_ = false;
                                promise->set_value(false);
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to open serial port {}: {}", config_.port, e.what());
                            isConnected_ = false;
                            promise->set_value(false);
                        }
                    });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in RTU connectAsync: {}", e.what());
                    promise->set_value(false);
                }

                return future;
            }

            std::future<void> RtuClient::disconnectAsync()
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
                            if (io_->serialPort && io_->serialPort->is_open()) {
                                std::error_code ec;
                                io_->serialPort->close(ec);
                                if (ec) {
                                    LOG_WARN("Error closing serial port: {}", ec.message());
                                }
                            }
                            decoder_.clear();
                            isConnected_ = false;
                            LOG_INFO("RTU client disconnected from port {}", config_.port);
                            promise->set_value();
                        } catch (const std::exception& e) {
                            LOG_ERROR("Exception in RTU disconnectAsync: {}", e.what());
                            promise->set_exception(std::current_exception());
                        }
                    });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in RTU disconnectAsync wrapper: {}", e.what());
                    promise->set_exception(std::current_exception());
                }

                return future;
            }

            std::future<std::vector<uint8_t>> RtuClient::sendRequestAsync(const std::vector<uint8_t>& frame)
            {
                LOG_INFO("TX: {}({})", dlt645::common::bytesToHexString(frame), frame.size());
                auto promise = std::make_shared<std::promise<std::vector<uint8_t>>>();
                auto future = promise->get_future();

                try {
                    if (!isConnected_ || !io_->serialPort || !io_->serialPort->is_open()) {
                        LOG_ERROR("RTU client not connected");
                        promise->set_value({});
                        return future;
                    }

                    auto buffer = std::make_shared<std::vector<uint8_t>>(frame);
                    auto serialPort = io_->serialPort;
                    auto traffic = io_->traffic;
                    const auto timeout = config_.timeout;

                    // 发送数据
                    asio::async_write(
                        *serialPort,
                        asio::buffer(*buffer),
                        [this, promise, buffer, serialPort, traffic, timeout](const std::error_code& error, std::size_t) {
                            if (traffic) {
                                observeTraffic(*traffic, TrafficDirection::send,
                                               { buffer->data(), buffer->size() }, error);
                            }
                            if (error) {
                                LOG_ERROR("RTU send failed: {}", error.message());
                                isConnected_ = false;
                                promise->set_value({});
                                return;
                            }

                            std::make_shared<transport::FrameReadOperation<asio::serial_port>>(
                                serialPort, decoder_, io_->context, timeout, promise, traffic)
                                ->start();
                        });
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception in RTU sendRequestAsync: {}", e.what());
                    promise->set_value({});
                }

                return future;
            }

            std::future<bool> RtuClient::sendOnlyAsync(const std::vector<uint8_t>& frame)
            {
                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();
                if (!isConnected_ || !io_->serialPort || !io_->serialPort->is_open()) {
                    promise->set_value(false);
                    return future;
                }
                auto buffer = std::make_shared<std::vector<uint8_t>>(frame);
                auto serialPort = io_->serialPort;
                auto traffic = io_->traffic;
                asio::async_write(*serialPort, asio::buffer(*buffer),
                                  [this, promise, buffer, serialPort, traffic](const std::error_code& error, size_t) {
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

            bool RtuClient::isConnected() const { return isConnected_; }

            void RtuClient::setTimeout(std::chrono::milliseconds timeout) { config_.timeout = timeout; }

        } // namespace client
    } // namespace transport
} // namespace dlt645
