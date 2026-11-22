#include <asio.hpp>
#include "dlt645/common/log.h"
#include "dlt645/transport/server/server_api.h"
#include "transport/observe_traffic.h"

namespace dlt645 {
    namespace transport {
        namespace server {

            constexpr uint64_t serialConnectionId = 1;

            struct RtuServer::Io {
                asio::io_context context;
                asio::serial_port port { context };
                asio::executor_work_guard<asio::io_context::executor_type> workGuard { asio::make_work_guard(context) };
            };

            RtuServer::RtuServer()
                : isRunning_(false)
                , receiveBuffer_(1024)
            {
            }

            RtuServer::~RtuServer()
            {
                stop();
            }

            bool RtuServer::configure(const RtuServerConfig& config)
            {
                if (isRunning_)
                    return false;
                config_ = config;
                return true;
            }

            bool RtuServer::configureSerialPort()
            {
                auto& port = io_->port;
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
                        LOG_WARN("Invalid stop bits value: {}", config.stopBits);
                        return false;
                    }

                    // 设置校验位
                    if (config.parity == "none") {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
                    } else if (config.parity == "even") {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::even));
                    } else if (config.parity == "odd") {
                        port.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::odd));
                    } else {
                        LOG_WARN("Invalid parity value: {}", config.parity);
                        return false;
                    }

                    // 设置流控制
                    if (config.flowControl == 0) {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
                    } else if (config.flowControl == 1) {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::software));
                    } else if (config.flowControl == 2) {
                        port.set_option(
                            asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::hardware));
                    } else {
                        LOG_WARN("Invalid flow control value: {}", config.flowControl);
                        return false;
                    }

                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to configure serial port: {}", e.what());
                    return false;
                }
            }

            bool RtuServer::start()
            {
                if (isRunning_) {
                    LOG_WARN("RTU server is already running");
                    return true;
                }

                try {
                    io_ = std::make_unique<Io>();
                    io_->port.open(config_.port);

                    if (!configureSerialPort()) {
                        LOG_ERROR("Failed to configure serial port: {}", config_.port);
                        io_.reset();
                        return false;
                    }

                    isRunning_ = true;
                    startReceive();

                    io_thread_ = std::thread([this]() {
                        try {
                            io_->context.run();
                        } catch (const std::exception& e) {
                            LOG_ERROR("RTU server IO context exception: {}", e.what());
                        }
                    });

                    LOG_INFO("RTU server started on port {}", config_.port);
                    return true;
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to start RTU server: {}", e.what());
                    stop();
                    return false;
                }
            }

            void RtuServer::stop()
            {
                if (!io_)
                    return;
                isRunning_ = false;
                if (io_thread_.joinable()) {
                    asio::post(io_->context, [this]() {
                        std::error_code ignored;
                        io_->port.close(ignored);
                        io_->workGuard.reset();
                    });
                    io_thread_.join();
                }
                io_.reset();
                decoder_.clear();
                LOG_INFO("RTU server stopped");
            }

            bool RtuServer::isRunning() const { return isRunning_; }

            void RtuServer::setConnectionHandler(std::shared_ptr<ConnectionHandler> handler) { connectionHandler_ = handler; }

            void RtuServer::startReceive()
            {
                if (!isRunning_ || !io_->port.is_open()) {
                    return;
                }

                io_->port.async_read_some(asio::buffer(receiveBuffer_),
                                          [this](const std::error_code& error, size_t bytes_transferred) {
                                              handleReceive(error, bytes_transferred);
                                          });
            }

            void RtuServer::handleReceive(const std::error_code& error, size_t bytes_transferred)
            {
                if (!error) {
                    if (bytes_transferred != 0) {
                        observeTraffic(config_.traffic, TrafficDirection::receive,
                                       { receiveBuffer_.data(), bytes_transferred }, {}, serialConnectionId);
                    }
                    if (!isRunning_)
                        return;
                    decoder_.append(receiveBuffer_.data(), bytes_transferred);
                    writeResponses();
                } else if (isRunning_) {
                    LOG_ERROR("RTU receive error: {}", error.message());
                    startReceive();

                    // 通知连接关闭
                    if (connectionHandler_) {
                        try {
                            connectionHandler_->onConnectionClosed();
                        } catch (const std::exception& e) {
                            LOG_ERROR("Exception in onConnectionClosed: {}", e.what());
                        }
                    }
                }
            }

            void RtuServer::writeResponses()
            {
                while (isRunning_ && connectionHandler_) {
                    auto frame = decoder_.nextFrame();
                    if (!frame)
                        break;
                    try {
                        auto response = connectionHandler_->handleRequest(*frame);
                        if (response.empty())
                            continue;
                        auto output = std::make_shared<std::vector<uint8_t>>(std::move(response));
                        asio::async_write(io_->port, asio::buffer(*output),
                                          [this, output](const std::error_code& error, size_t) {
                                              observeTraffic(config_.traffic, TrafficDirection::send,
                                                             { output->data(), output->size() }, error, serialConnectionId);
                                              if (error && isRunning_)
                                                  LOG_ERROR("Failed to send RTU response: {}", error.message());
                                              writeResponses();
                                          });
                        return;
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to handle RTU frame: {}", e.what());
                    }
                }
                startReceive();
            }

        } // namespace server
    } // namespace transport
} // namespace dlt645
