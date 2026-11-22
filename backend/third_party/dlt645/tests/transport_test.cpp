#include "dlt645/protocol/protocol.h"
#include "dlt645/service/client_service.h"
#include "dlt645/service/server_service.h"
#include "dlt645/transport/client/client_api.h"
#include "dlt645/transport/server/server_api.h"
#include "log/log_init.hpp"
#include <asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
    using asio::ip::tcp;
    using dlt645::protocol::Frame;
    using dlt645::transport::TrafficDirection;
    using dlt645::transport::TrafficEvent;
    constexpr std::array<uint8_t, 6> address { 0x12, 0x34, 0x56, 0x78, 0x10, 0x12 };

    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    class EchoHandler : public dlt645::transport::server::ConnectionHandler {
    public:
        std::vector<uint8_t> handleRequest(const Frame& frame) override
        {
            ++requests;
            return Frame::buildFrame(frame.addr, frame.ctrlCode | 0x80, frame.data);
        }
        void onConnectionClosed() override {}
        std::atomic<int> requests { 0 };
    };

    void testServerStream()
    {
        asio::io_context context;
        tcp::acceptor reserve(context, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        const auto port = reserve.local_endpoint().port();
        reserve.close();

        std::vector<uint8_t> received;
        std::vector<uint8_t> sent;
        bool trafficOk = true;
        dlt645::transport::server::TcpServer server;
        dlt645::transport::server::TcpServerConfig config;
        config.ip = "127.0.0.1";
        config.port = port;
        config.traffic = [&](uint64_t id, const TrafficEvent& event) {
            trafficOk = trafficOk && id == 1 && static_cast<bool>(event.result)
                && event.timestamp != std::chrono::system_clock::time_point {};
            auto& bytes = event.direction == TrafficDirection::receive ? received : sent;
            bytes.insert(bytes.end(), event.bytes.begin(), event.bytes.end());
        };
        check(server.configure(config), "configure server");
        auto handler = std::make_shared<EchoHandler>();
        server.setConnectionHandler(handler);
        check(server.start(), "start server");

        tcp::socket socket(context);
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
        const auto request = Frame::buildFrame(address, 0x11, { 0, 0, 0, 0 });
        const std::array<uint8_t, 2> noise { 0x00, 0x01 };
        asio::write(socket, asio::buffer(noise));
        asio::write(socket, asio::buffer(request.data(), 8));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::vector<uint8_t> rest(request.begin() + 8, request.end());
        rest.insert(rest.end(), request.begin(), request.end());
        asio::write(socket, asio::buffer(rest));

        socket.non_blocking(true);
        dlt645::protocol::FrameStreamDecoder decoder;
        std::array<uint8_t, 256> bytes {};
        int responses = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (responses < 2 && std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            const auto size = socket.read_some(asio::buffer(bytes), error);
            if (!error) {
                decoder.append(bytes.data(), size);
                while (decoder.nextFrame())
                    ++responses;
            } else if (error != asio::error::would_block) {
                throw std::system_error(error);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        check(responses == 2 && handler->requests == 2, "server handles split and coalesced frames");
        socket.close();
        server.stop();

        std::vector<uint8_t> expectedRx(noise.begin(), noise.end());
        expectedRx.insert(expectedRx.end(), request.begin(), request.end());
        expectedRx.insert(expectedRx.end(), request.begin(), request.end());
        const auto response = Frame::buildFrame(address, 0x91, { 0, 0, 0, 0 });
        auto expectedTx = response;
        expectedTx.insert(expectedTx.end(), response.begin(), response.end());
        check(trafficOk && received == expectedRx && sent == expectedTx, "server reports raw RX and TX with connection ID");
    }

    void testClientFragmentedResponse()
    {
        asio::io_context context;
        tcp::acceptor acceptor(context, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        const auto port = acceptor.local_endpoint().port();
        const auto response = Frame::buildFrame(address, 0x91, { 0, 0, 0, 0, 0, 0, 0, 0 });
        auto damaged = response;
        damaged[damaged.size() - 2] ^= 1;
        std::exception_ptr serverError;
        std::thread peer([&] {
            try {
                tcp::socket socket(context);
                acceptor.accept(socket);
                std::array<uint8_t, 128> bytes {};
                check(socket.read_some(asio::buffer(bytes)) > 0, "client request received");
                asio::write(socket, asio::buffer(damaged));
                asio::write(socket, asio::buffer(response.data(), 7));
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                asio::write(socket, asio::buffer(response.data() + 7, response.size() - 7));
            } catch (...) {
                serverError = std::current_exception();
            }
        });

        try {
            std::vector<uint8_t> received;
            std::vector<uint8_t> sent;
            dlt645::transport::client::TcpClient client;
            dlt645::transport::client::TcpClientConfig config;
            config.ip = "127.0.0.1";
            config.port = port;
            config.timeout = std::chrono::seconds(2);
            config.traffic = [&](const TrafficEvent& event) {
                auto& bytes = event.direction == TrafficDirection::receive ? received : sent;
                bytes.insert(bytes.end(), event.bytes.begin(), event.bytes.end());
                throw std::runtime_error("traffic callback exception");
            };
            check(client.configure(config) && client.connect(), "client connects");
            const auto request = Frame::buildFrame(address, 0x11, { 0, 0, 0, 0 });
            const auto reply = client.sendRequest(request);
            check(Frame::deserialize(reply) != nullptr, "client reassembles response");
            client.disconnect();
            auto expectedRx = damaged;
            expectedRx.insert(expectedRx.end(), response.begin(), response.end());
            check(sent == request && received == expectedRx, "client reports TX and unparsed RX despite callback exceptions");
        } catch (...) {
            peer.join();
            throw;
        }
        peer.join();
        if (serverError)
            std::rethrow_exception(serverError);
    }

    void testServiceTraffic()
    {
        using namespace dlt645::service;
        asio::io_context context;
        tcp::acceptor reserve(context, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        const auto port = reserve.local_endpoint().port();
        reserve.close();

        std::vector<uint64_t> responseIds;
        ServerOptions serverOptions;
        serverOptions.traffic = [&](uint64_t id, const TrafficEvent& event) {
            if (event.direction == TrafficDirection::send && event.result)
                responseIds.push_back(id);
        };
        auto server = createTcpServer("127.0.0.1", port, serverOptions);
        check(server && server->start(), "start service with ServerOptions.traffic");
        try {
            for (int connection = 0; connection < 2; ++connection) {
                int sent = 0;
                ClientOptions clientOptions;
                clientOptions.timeout = std::chrono::seconds(2);
                clientOptions.traffic = [&](const TrafficEvent& event) {
                    if (event.direction == TrafficDirection::send && event.result)
                        ++sent;
                };
                auto client = ClientService::createTcpClient("127.0.0.1", port, clientOptions);
                check(client && client->connect(), "connect service with ClientOptions.traffic");
                check(client->broadcastTimeSync(), "send one-way broadcast through service");
                check(static_cast<bool>(client->readAddress()) && sent == 2, "observe both one-way and request writes");
                client->disconnect();
            }
        } catch (...) {
            server->stop();
            throw;
        }
        server->stop();
        check(responseIds == std::vector<uint64_t>({ 1, 2 }), "service reports distinct TCP connection IDs");
    }

    void testCanceledWriteTraffic()
    {
        asio::io_context context;
        tcp::acceptor acceptor(context, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        dlt645::transport::client::TcpClientConfig config;
        config.port = acceptor.local_endpoint().port();
        const std::vector<uint8_t> payload(8 * 1024 * 1024, 0xFE);
        int writes = 0;
        bool failedWriteReported = false;
        config.traffic = [&](const TrafficEvent& event) {
            ++writes;
            failedWriteReported = event.direction == TrafficDirection::send && !event.result
                && event.result.error() && event.bytes.size() == payload.size()
                && std::equal(event.bytes.begin(), event.bytes.end(), payload.begin());
        };

        dlt645::transport::client::TcpClient client;
        check(client.configure(config), "configure client for write cancellation");
        check(!client.sendOnly({ 0xFE }) && writes == 0, "no event for a write that was never submitted");
        check(client.connect(), "connect client for write cancellation");
        tcp::socket peer(context);
        acceptor.accept(peer);
        peer.set_option(asio::socket_base::receive_buffer_size(4096));

        auto pending = client.sendOnlyAsync(payload);
        client.disconnect();
        check(!pending.get() && writes == 1 && failedWriteReported, "canceled write reports all submitted bytes and error");
    }

    void testClientTimeoutRecovery()
    {
        asio::io_context context;
        tcp::acceptor acceptor(context, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
        const auto port = acceptor.local_endpoint().port();
        std::exception_ptr serverError;
        std::thread peer([&] {
            try {
                tcp::socket socket(context);
                acceptor.accept(socket);
                std::array<uint8_t, 128> bytes {};
                check(socket.read_some(asio::buffer(bytes)) > 0, "first request received");
                const auto response = Frame::buildFrame(address, 0x91, { 0, 0, 0, 0, 0, 0, 0, 0 });
                asio::write(socket, asio::buffer(response.data(), 7));
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                check(socket.read_some(asio::buffer(bytes)) > 0, "second request received");
                asio::write(socket, asio::buffer(response));
            } catch (...) {
                serverError = std::current_exception();
            }
        });

        try {
            dlt645::transport::client::TcpClient client;
            dlt645::transport::client::TcpClientConfig config;
            config.ip = "127.0.0.1";
            config.port = port;
            config.timeout = std::chrono::milliseconds(300);
            check(client.configure(config) && client.connect(), "client connects for timeout test");
            const auto request = Frame::buildFrame(address, 0x11, { 0, 0, 0, 0 });
            check(client.sendRequest(request).empty(), "partial response times out");
            check(Frame::deserialize(client.sendRequest(request)) != nullptr,
                  "next request succeeds after partial response timeout");
            client.disconnect();
        } catch (...) {
            peer.join();
            throw;
        }
        peer.join();
        if (serverError)
            std::rethrow_exception(serverError);
    }
} // namespace

int main()
{
    try {
        LogInitializer logger("dlt645_transport_test", LV::info, false);
        testServerStream();
        testClientFragmentedResponse();
        testClientTimeoutRecovery();
        testServiceTraffic();
        testCanceledWriteTraffic();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
