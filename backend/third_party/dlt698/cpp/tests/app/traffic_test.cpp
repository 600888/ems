/** @file traffic_test.cpp
 * @brief 托管收发观察、连接标识及异常隔离的真实 TCP 回环验证。
 */
#include <dlt698/app.hpp>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::app;

namespace {
struct StreamLog {
    Bytes received, sent;
    bool success = true;
    bool timestamp_valid = true;
    std::thread::id thread;

    /// 在回调内复制字节；同一连接的 RX 块合并后与对端 TX 流比较。
    void append(const session::TrafficEvent& event) {
        auto& bytes = event.direction == session::TrafficDirection::receive ? received : sent;
        bytes.insert(bytes.end(), event.bytes.data(), event.bytes.data() + event.bytes.size());
        success = success && static_cast<bool>(event.result);
        timestamp_valid = timestamp_valid && event.timestamp.time_since_epoch().count() != 0;
        thread = std::this_thread::get_id();
    }
};
}  // namespace

TEST_CASE("托管收发回调包含握手业务释放且服务器区分连接", "[app][traffic]") {
    std::mutex mutex;
    std::map<std::uint64_t, StreamLog> server_logs;
    StreamLog client_logs[2];
    const auto caller = std::this_thread::get_id();
    ServerOptions server_options;
    server_options.traffic = [&](std::uint64_t id, const session::TrafficEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        server_logs[id].append(event);
        throw std::runtime_error("server observer failed");
    };
    Server server(server_options);
    REQUIRE(server.set({0x200F, 2, 0}, model::UInt16{5000}));
    REQUIRE(server.start_tcp("127.0.0.1", 0));
    for (int i = 0; i < 2; ++i) {
        ClientOptions client_options;
        client_options.traffic = [&, i](const session::TrafficEvent& event) {
            client_logs[i].append(event);
            throw std::runtime_error("client observer failed");
        };
        Client client(client_options);
        REQUIRE(client.connect_tcp("127.0.0.1", server.local_port()));
        auto response = client.get({0x200F, 2, 0});
        REQUIRE(response);
        CHECK(std::get<model::Data>(response.value()).as<model::UInt16>().value == 5000);
        REQUIRE(client.disconnect());
    }
    REQUIRE(server.stop());
    // 所有运行线程收尾后检查副本，无需在观察器中阻塞或跨线程借用 ByteView。
    REQUIRE(server_logs.size() == 2);
    int index = 0;
    for (const auto& entry : server_logs) {
        CHECK(entry.first != 0);
        const auto& server_log = entry.second;
        const auto& client_log = client_logs[index++];
        CHECK_FALSE(client_log.sent.empty());
        CHECK_FALSE(client_log.received.empty());
        CHECK(client_log.sent == server_log.received);
        CHECK(client_log.received == server_log.sent);
        CHECK(client_log.success);
        CHECK(server_log.success);
        CHECK(client_log.timestamp_valid);
        CHECK(server_log.timestamp_valid);
        CHECK(client_log.thread != caller);
        CHECK(server_log.thread != caller);
        protocol::link::FrameStreamDecoder decoder;
        const auto events = decoder.feed(client_log.sent);
        // 默认远程场景中客户机至少发送 LINK 应答、CONNECT、GET 和 RELEASE。
        CHECK(events.size() >= 4);
        for (const auto& event : events)
            CHECK(std::holds_alternative<protocol::link::Frame>(event));
    }
}
