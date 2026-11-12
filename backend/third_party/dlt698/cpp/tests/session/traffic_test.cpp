/** @file traffic_test.cpp
 * @brief 收发观察的字节边界、写入结果、时间和关闭生命周期验证。
 */
#include <dlt698/session/session.hpp>
#include <stdexcept>
#include <utility>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::session;

namespace {
struct SavedTraffic {
    TrafficDirection direction;
    std::chrono::system_clock::time_point timestamp;
    Bytes bytes;
    Result<void> result;
};

/// 保存拥有内存的副本，验证借用视图离开回调后的使用方式。
SavedTraffic save(const TrafficEvent& event) {
    return {event.direction, event.timestamp,
            Bytes(event.bytes.data(), event.bytes.data() + event.bytes.size()), event.result};
}

/** @brief 可由测试精确控制读取块与写完成时刻的通道。 */
struct ControlledChannel : transport::IChannel {
    ReadHandler reader;
    WriteHandler writer;
    Bytes written;

    /** @brief 保留读取完成回调，等待测试注入字节块。
     * @param[in] handler 由测试完成的读取回调。
     */
    void async_read(ReadHandler handler) override { reader = std::move(handler); }

    /** @brief 接管写入数据，不提前报告发送成功。
     * @param[in] bytes 完整待写字节。
     * @param[in] handler 由测试完成的写入回调。
     */
    void async_write(Bytes bytes, WriteHandler handler) override {
        written = std::move(bytes);
        writer = std::move(handler);
    }

    /** @brief 结束挂起读写，模拟通道关闭时的取消完成。 */
    void close() override {
        if (reader) std::exchange(reader, {})(Error{ErrorCode::closed, 0, "read cancelled"});
        if (writer) std::exchange(writer, {})(Error{ErrorCode::closed, 0, "write cancelled"});
    }
};

struct Fixture {
    std::vector<SavedTraffic> events;
    std::shared_ptr<ManualExecutor> executor = std::make_shared<ManualExecutor>();
    std::shared_ptr<ControlledChannel> channel = std::make_shared<ControlledChannel>();
    std::shared_ptr<Session> session = std::make_shared<Session>(channel, executor);

    /** @brief 在接收泵启动前注册观察器。 */
    Fixture() {
        session->set_traffic_handler(
            [this](const TrafficEvent& event) { events.push_back(save(event)); });
        session->start();
        executor->run_ready();
    }

    /** @brief 驱动关闭及迟到完成，保证观察器不会借用已销毁的测试数据。 */
    ~Fixture() {
        if (session) session->close();
        executor->run_ready();
    }
};
}  // namespace

TEST_CASE("发送观察等待实际写完成并保留提交时的观察器", "[session][traffic]") {
    Fixture fixture;
    fixture.session->async_connect([](auto) {});
    fixture.executor->run_ready();
    REQUIRE(fixture.channel->writer);
    const auto expected = fixture.channel->written;
    REQUIRE(protocol::link::decode_frame(expected));
    CHECK(fixture.events.empty());
    fixture.session->set_traffic_handler({});
    fixture.executor->run_ready();
    const auto before = std::chrono::system_clock::now();
    std::exchange(fixture.channel->writer, {})(Result<void>{});
    const auto after = std::chrono::system_clock::now();
    CHECK(fixture.events.empty());
    fixture.executor->run_ready();
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events[0].direction == TrafficDirection::send);
    CHECK(fixture.events[0].bytes == expected);
    CHECK(fixture.events[0].result);
    CHECK(fixture.events[0].timestamp >= before);
    CHECK(fixture.events[0].timestamp <= after);
}

TEST_CASE("写失败观察保留原字节和错误且不改变事务失败", "[session][traffic]") {
    Fixture fixture;
    std::optional<Result<protocol::apdu::ConnectResponse>> response;
    fixture.session->async_connect([&](auto result) { response = std::move(result); });
    fixture.executor->run_ready();
    const auto expected = fixture.channel->written;
    REQUIRE(fixture.channel->writer);
    std::exchange(fixture.channel->writer, {})(Error{ErrorCode::resource_limit, 7, "write budget"});
    fixture.executor->run_ready();
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events[0].bytes == expected);
    REQUIRE_FALSE(fixture.events[0].result);
    CHECK(fixture.events[0].result.error().code == ErrorCode::resource_limit);
    CHECK(fixture.events[0].result.error().offset == 7);
    CHECK(fixture.events[0].result.error().context == "write budget");
    REQUIRE(response);
    CHECK_FALSE(*response);
    CHECK(response->error().code == ErrorCode::resource_limit);
    CHECK(fixture.session->state() == State::closed);
}

TEST_CASE("会话销毁仍交付已提交发送的取消结果", "[session][traffic]") {
    Fixture fixture;
    fixture.session->async_connect([](auto) {});
    fixture.executor->run_ready();
    const auto expected = fixture.channel->written;
    fixture.session.reset();
    fixture.executor->run_ready();
    REQUIRE(fixture.events.size() == 1);
    CHECK(fixture.events[0].bytes == expected);
    CHECK_FALSE(fixture.events[0].result);
    CHECK(fixture.events[0].result.error().code == ErrorCode::closed);
}

TEST_CASE("接收观察保留半帧粘帧及损坏字节且隔离回调异常", "[session][traffic]") {
    Fixture fixture;
    std::vector<Error> diagnostics;
    fixture.session->set_diagnostic_handler(
        [&](const Error& error) { diagnostics.push_back(error); });
    fixture.session->set_traffic_handler([&](const TrafficEvent& event) {
        fixture.events.push_back(save(event));
        throw std::runtime_error("observer failed");
    });
    fixture.executor->run_ready();
    protocol::link::Frame frame;
    frame.server.bytes = {0};
    frame.control = 0xc3;
    frame.payload = {0xff};
    const auto encoded = protocol::link::encode_frame(frame);
    REQUIRE(encoded);
    const Bytes wire = encoded.value();
    Bytes first{0xfe, 0xfe, 0xfe, 0xfe};
    first.insert(first.end(), wire.begin(), wire.begin() + 5);
    Bytes second(wire.begin() + 5, wire.end());
    Bytes broken = wire;
    broken[broken.size() - 2] ^= 1;
    second.insert(second.end(), broken.begin(), broken.end());
    for (const auto& bytes : {first, second}) {
        REQUIRE(fixture.channel->reader);
        std::exchange(fixture.channel->reader, {})(bytes);
        fixture.executor->run_ready();
    }
    REQUIRE(fixture.events.size() == 2);
    CHECK(fixture.events[0].bytes == first);
    CHECK(fixture.events[1].bytes == second);
    for (const auto& event : fixture.events) {
        CHECK(event.direction == TrafficDirection::receive);
        CHECK(event.result);
    }
    bool checksum_seen = false;
    for (const auto& diagnostic : diagnostics)
        if (diagnostic.code == ErrorCode::checksum_frame) checksum_seen = true;
    CHECK(checksum_seen);
    CHECK(fixture.session->state() != State::closed);
    CHECK(static_cast<bool>(fixture.channel->reader));
    fixture.session->set_traffic_handler({});
    fixture.executor->run_ready();
    std::exchange(fixture.channel->reader, {})(wire);
    fixture.executor->run_ready();
    CHECK(fixture.events.size() == 2);
}
