#ifndef DLT645_SERVER_API_H
#define DLT645_SERVER_API_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dlt645/protocol/protocol.h"
#include "dlt645/transport/traffic.h"

namespace dlt645 {
    namespace transport {
        namespace server {

            // 前向声明
            class TcpServer;
            class RtuServer;

            // 连接处理器接口定义
            class ConnectionHandler {
            public:
                virtual ~ConnectionHandler() = default;

                // 处理接收到的数据
                virtual std::vector<uint8_t> handleRequest(const protocol::Frame& frame) = 0;

                // 连接关闭时的回调
                virtual void onConnectionClosed() = 0;
            };

            // 服务器配置基类
            struct ServerOptions {
                std::chrono::milliseconds timeout = std::chrono::seconds(5); // 默认超时时间5秒
                ServerTrafficHandler traffic {};
            };
            using ServerConfig = ServerOptions;

            // TCP服务器配置
            struct TcpServerConfig : public ServerConfig {
                std::string ip = "0.0.0.0";
                uint16_t port = 10521;
                int maxConnections = 10; // 最大连接数
            };

            // RTU服务器配置
            struct RtuServerConfig : public ServerConfig {
                std::string port = "/dev/ttyS0";
                int baudRate = 9600;
                int dataBits = 8;
                int stopBits = 1;
                std::string parity = "none"; // "none", "even", "odd"
                int flowControl = 0;         // 0: none, 1: software, 2: hardware
            };

            // 服务器接口定义
            class Server {
            public:
                virtual ~Server() = default;

                // 启动服务器
                virtual bool start() = 0;

                // 停止服务器
                virtual void stop() = 0;

                // 检查服务器是否运行
                virtual bool isRunning() const = 0;

                // 设置连接处理器
                virtual void setConnectionHandler(std::shared_ptr<ConnectionHandler> handler) = 0;
            };

            // TCP服务器实现
            class TcpServer : public Server {
            public:
                TcpServer();
                ~TcpServer();

                // 配置TCP服务器
                bool configure(const TcpServerConfig& config);

                // 实现Server接口
                bool start() override;
                void stop() override;
                bool isRunning() const override;
                void setConnectionHandler(std::shared_ptr<ConnectionHandler> handler) override;

            private:
                struct Io;
                struct ClientSession;
                TcpServerConfig config_;
                std::unique_ptr<Io> io_;
                std::atomic<bool> isRunning_;
                std::shared_ptr<ConnectionHandler> connectionHandler_;
                std::thread io_thread_;

                // 接受连接
                void acceptConnection();

                // 处理客户端连接
                void handleClient(std::shared_ptr<ClientSession> client);
                void closeClient(uint64_t id);
            };

            // RTU服务器实现
            class RtuServer : public Server {
            public:
                RtuServer();
                ~RtuServer();

                // 配置RTU服务器
                bool configure(const RtuServerConfig& config);

                // 实现Server接口
                bool start() override;
                void stop() override;
                bool isRunning() const override;
                void setConnectionHandler(std::shared_ptr<ConnectionHandler> handler) override;

            private:
                struct Io;
                RtuServerConfig config_;
                std::unique_ptr<Io> io_;
                std::atomic<bool> isRunning_;
                std::shared_ptr<ConnectionHandler> connectionHandler_;
                std::thread io_thread_;
                std::vector<uint8_t> receiveBuffer_;
                protocol::FrameStreamDecoder decoder_;

                // 配置串口参数
                bool configureSerialPort();

                // 开始接收数据
                void startReceive();

                // 处理接收到的数据
                void handleReceive(const std::error_code& error, size_t bytes_transferred);
                void writeResponses();
            };

        } // namespace server
    } // namespace transport
} // namespace dlt645

#endif // DLT645_SERVER_API_H
