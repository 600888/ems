#include "dlt645/transport/server/server_api.h"
#include <asio.hpp>
#include <array>
#include <unordered_map>
#include "dlt645/common/log.h"
#include "transport/observe_traffic.h"

namespace dlt645::transport::server {

    struct TcpServer::ClientSession {
        explicit ClientSession(asio::io_context& context) : socket(context) {}

        asio::ip::tcp::socket socket;
        protocol::FrameStreamDecoder decoder;
        std::array<uint8_t, 1024> input {};
        uint64_t id = 0;
    };

    struct TcpServer::Io {
        explicit Io(const asio::ip::tcp::endpoint& endpoint) : acceptor(context, endpoint) {}

        asio::io_context context;
        asio::ip::tcp::acceptor acceptor;
        asio::executor_work_guard<asio::io_context::executor_type> workGuard { asio::make_work_guard(context) };
        std::unordered_map<uint64_t, std::shared_ptr<ClientSession>> clients;
        uint64_t nextId = 1;
    };

    TcpServer::TcpServer() : isRunning_(false) {}

    TcpServer::~TcpServer() { stop(); }

    bool TcpServer::configure(const TcpServerConfig& config)
    {
        if (isRunning_)
            return false;
        config_ = config;
        return true;
    }

    bool TcpServer::start()
    {
        if (isRunning_) {
            LOG_WARN("TCP server is already running");
            return true;
        }

        try {
            const asio::ip::tcp::endpoint endpoint(asio::ip::make_address(config_.ip), config_.port);
            io_ = std::make_unique<Io>(endpoint);
            isRunning_ = true;
            acceptConnection();
            io_thread_ = std::thread([this]() {
                try {
                    io_->context.run();
                } catch (const std::exception& e) {
                    LOG_ERROR("TCP server IO context exception: {}", e.what());
                }
            });
            LOG_INFO("TCP server started on {}:{}", config_.ip, config_.port);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to start TCP server: {}", e.what());
            stop();
            return false;
        }
    }

    void TcpServer::stop()
    {
        if (!io_)
            return;
        isRunning_ = false;
        if (io_thread_.joinable()) {
            asio::post(io_->context, [this]() {
                std::error_code ignored;
                io_->acceptor.close(ignored);
                for (const auto& entry : io_->clients)
                    entry.second->socket.close(ignored);
                io_->workGuard.reset();
            });
            io_thread_.join();
        }
        io_.reset();
        LOG_INFO("TCP server stopped");
    }

    bool TcpServer::isRunning() const { return isRunning_; }

    void TcpServer::setConnectionHandler(std::shared_ptr<ConnectionHandler> handler) { connectionHandler_ = std::move(handler); }

    void TcpServer::acceptConnection()
    {
        auto client = std::make_shared<ClientSession>(io_->context);
        io_->acceptor.async_accept(client->socket, [this, client](const std::error_code& error) {
            if (!isRunning_)
                return;
            if (!error) {
                try {
                    std::error_code remoteError;
                    const auto remote = client->socket.remote_endpoint(remoteError);
                    if (!remoteError)
                        LOG_INFO("New TCP connection from {}", remote.address().to_string());
                    client->id = io_->nextId++;
                    io_->clients.emplace(client->id, client);
                    handleClient(client);
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to handle new TCP connection: {}", e.what());
                    closeClient(client->id);
                }
            } else {
                LOG_ERROR("Failed to accept TCP connection: {}", error.message());
            }
            if (isRunning_)
                acceptConnection();
        });
    }

    void TcpServer::closeClient(uint64_t id)
    {
        io_->clients.erase(id);
        if (isRunning_ && connectionHandler_) {
            try {
                connectionHandler_->onConnectionClosed();
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in onConnectionClosed: {}", e.what());
            }
        }
    }

    void TcpServer::handleClient(std::shared_ptr<ClientSession> client)
    {
        client->socket.async_read_some(
            asio::buffer(client->input),
            [this, client](const std::error_code& error, size_t bytesRead) {
                if (error) {
                    if (isRunning_)
                        LOG_INFO("TCP client disconnected: {}", error.message());
                    closeClient(client->id);
                    return;
                }
                if (bytesRead != 0) {
                    observeTraffic(config_.traffic, TrafficDirection::receive,
                                   { client->input.data(), bytesRead }, {}, client->id);
                }
                if (!isRunning_)
                    return;
                try {
                    client->decoder.append(client->input.data(), bytesRead);
                    std::vector<uint8_t> responses;
                    while (auto frame = client->decoder.nextFrame()) {
                        if (!connectionHandler_)
                            continue;
                        try {
                            auto response = connectionHandler_->handleRequest(*frame);
                            responses.insert(responses.end(), response.begin(), response.end());
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to handle TCP frame: {}", e.what());
                        }
                    }
                    if (responses.empty()) {
                        if (isRunning_)
                            handleClient(client);
                        return;
                    }
                    auto output = std::make_shared<std::vector<uint8_t>>(std::move(responses));
                    asio::async_write(
                        client->socket, asio::buffer(*output),
                        [this, client, output](const std::error_code& writeError, size_t) {
                            observeTraffic(config_.traffic, TrafficDirection::send,
                                           { output->data(), output->size() }, writeError, client->id);
                            if (writeError) {
                                if (isRunning_)
                                    LOG_ERROR("Failed to send TCP response: {}", writeError.message());
                                closeClient(client->id);
                            } else if (isRunning_) {
                                handleClient(client);
                            }
                        });
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to process TCP input: {}", e.what());
                    if (isRunning_)
                        handleClient(client);
                }
            });
    }

} // namespace dlt645::transport::server
