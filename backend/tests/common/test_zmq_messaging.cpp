#include <common/messaging/zmq_command_channel.h>
#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <string>

namespace {

TEST(ZmqCommandChannelTest, RoundTripAndStableIdRetry) {
    ems::messaging::ZmqCommandConfig config;
    config.endpoint = "tcp://127.0.0.1:*";
    config.request_timeout_ms = 2000;

    std::atomic<int> deliveries{0};
    ems::messaging::ZmqCommandServer server;
    ASSERT_TRUE(server.start(config, [&deliveries](const auto& request) {
        if (request.command_id == "cmd_throw") throw std::runtime_error("uncertain commit");
        EXPECT_EQ(request.command_id, "cmd_test_01");
        EXPECT_EQ(request.payload_json, R"({"command_id":"cmd_test_01"})");
        const int count = ++deliveries;
        return ems::messaging::ZmqCommandReply{count == 1
                                                   ? ems::messaging::ZmqCommandReplyCode::Accepted
                                                   : ems::messaging::ZmqCommandReplyCode::Duplicate,
                                               "registered"};
    }));
    ASSERT_TRUE(server.running());

    config.endpoint = server.bound_endpoint();
    ASSERT_FALSE(config.endpoint.empty());
    ems::messaging::ZmqCommandClient client(config);
    const ems::messaging::ZmqCommandRequest command{"cmd_test_01",
                                                    R"({"command_id":"cmd_test_01"})"};
    EXPECT_EQ(client.send(command).code, ems::messaging::ZmqCommandReplyCode::Accepted);
    EXPECT_EQ(client.send(command).code, ems::messaging::ZmqCommandReplyCode::Duplicate);
    EXPECT_EQ(deliveries, 2);

    EXPECT_EQ(client.send({"invalid/id", "{}"}).code, ems::messaging::ZmqCommandReplyCode::Invalid);
    EXPECT_EQ(client.send({"cmd_throw", "{}"}).code, ems::messaging::ZmqCommandReplyCode::Unknown);
    EXPECT_EQ(deliveries, 2);
    server.stop();
    EXPECT_FALSE(server.running());
    EXPECT_TRUE(server.bound_endpoint().empty());
    config.request_timeout_ms = 100;
    ems::messaging::ZmqCommandClient disconnected(config);
    EXPECT_EQ(disconnected.send(command).code, ems::messaging::ZmqCommandReplyCode::Unknown);
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
