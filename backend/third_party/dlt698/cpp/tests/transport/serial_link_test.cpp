/**
 * @file serial_link_test.cpp
 * @brief 串行链路适配的方向切换、帧间隔与排空预算测试。
 * @note 使用内存通道与虚拟时钟，不依赖真实串口硬件。
 */
#include <chrono>
#include <dlt698/dlt698.hpp>
#include <dlt698/transport/memory.hpp>
#include <dlt698/transport/serial_link.hpp>
#include <vector>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::transport;
using namespace std::chrono_literals;
using dlt698::test::hex;

namespace {

/// 分多次推进虚拟时钟，直到写入完成或达到上限。
/// 帧间隔、排空估算与队列后续是链式定时，单次推进只能触发其中一环。
void settle(const std::shared_ptr<ManualExecutor>& executor, int rounds = 12) {
    for (int i = 0; i < rounds; ++i) executor->advance(std::chrono::minutes(1));
}

/// 生成一个最小的合法链路帧，作为适配器的输入。
Bytes sample_frame() {
    protocol::link::Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    const Bytes payload = hex("05 01 01 40 01 02 00");
    frame.payload = payload;
    auto bytes = protocol::link::encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    return bytes.value();
}

}  // namespace

TEST_CASE("串行链路为每帧添加前导FE", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);
    SerialLinkOptions options;
    options.baud_rate = 9600;
    auto adapter = SerialLinkChannel::wrap(pair.first, executor, options);

    std::optional<Result<Bytes>> received;
    pair.second->async_read([&](Result<Bytes> r) { received = std::move(r); });

    const Bytes frame = sample_frame();
    std::optional<Result<void>> written;
    adapter->async_write(frame, [&](Result<void> r) { written = std::move(r); });
    executor->run_ready();
    // 帧间隔以虚拟时钟计时，推进后写入才完成。
    settle(executor);

    REQUIRE(received.has_value());
    // optional<Result<Bytes>> 需先取出 Result 再解包，两步都要显式。
    REQUIRE(static_cast<bool>(received.value()));
    const Bytes wire = std::move(received).value().value();
    REQUIRE_FALSE(wire.empty());
    // 前导 FE 不参与帧结构，剥离后应还原出原始帧。
    std::size_t offset = 0;
    while (offset < wire.size() && wire[offset] == 0xfe) ++offset;
    REQUIRE(offset > 0);  // 帧前必须有 FE 分隔，否则接收端无法定帧。
    CHECK(offset >= 1);
    CHECK(to_hex(ByteView{wire}.subview(offset, wire.size() - offset)) == to_hex(ByteView{frame}));
    REQUIRE(written.has_value());
    CHECK(static_cast<bool>(*written));
}

TEST_CASE("串行链路提供排空回调时按真实时序完成", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);

    SerialLinkOptions options;
    options.baud_rate = 1000;  // 每位10ms，11 位即 110ms 一字节，便于观察计时行为。
    options.bits_per_character = 11;
    std::vector<bool> directions;
    IChannel::WriteHandler drain;
    options.set_transmit = [&](bool tx) -> Result<void> {
        directions.push_back(tx);
        return {};
    };
    options.async_drain = [&](IChannel::WriteHandler callback) { drain = std::move(callback); };

    auto adapter = SerialLinkChannel::wrap(pair.first, executor, options);
    const Bytes frame = sample_frame();
    std::optional<Result<void>> written;
    adapter->async_write(frame, [&](Result<void> r) { written = std::move(r); });
    executor->run_ready();

    // 排空回调尚未交付，写入不得提前完成。
    REQUIRE_FALSE(written.has_value());
    REQUIRE(drain);
    drain(Result<void>{});
    executor->run_ready();
    // 排空确认后才开始 33 位静默计时，因此回调仍未交付。
    REQUIRE_FALSE(written.has_value());

    settle(executor);
    REQUIRE(written.has_value());
    CHECK(static_cast<bool>(*written));
    // 方向切换顺序：先置发送，排空后再回到接收。
    REQUIRE(directions.size() >= 2);
    CHECK(directions.front() == true);
    CHECK(directions.back() == false);
}

TEST_CASE("没有排空回调时以波特率估算帧间隔", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);

    SerialLinkOptions options;
    options.baud_rate = 9600;
    options.bits_per_character = 11;
    auto adapter = SerialLinkChannel::wrap(pair.first, executor, options);

    const Bytes frame = sample_frame();
    std::optional<Result<void>> written;
    adapter->async_write(frame, [&](Result<void> r) { written = std::move(r); });
    executor->run_ready();

    // 估算模式下帧间隔通过虚拟时钟推进，不会立即完成。
    REQUIRE_FALSE(written.has_value());
    settle(executor);
    REQUIRE(written.has_value());
    CHECK(static_cast<bool>(*written));
}

TEST_CASE("串行链路拒绝无效线路参数", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);

    SECTION("波特率为零被拒绝") {
        SerialLinkOptions options;
        options.baud_rate = 0;
        CHECK_THROWS_AS(SerialLinkChannel::wrap(pair.first, executor, options),
                        std::invalid_argument);
    }

    SECTION("每字符位数不足两个被拒绝") {
        SerialLinkOptions options;
        options.bits_per_character = 1;
        CHECK_THROWS_AS(SerialLinkChannel::wrap(pair.first, executor, options),
                        std::invalid_argument);
    }

    SECTION("配置了方向切换但没有排空回调被拒绝") {
        // 手动方向切换必须有真实 async_drain，否则无法保证硬件时序。
        SerialLinkOptions options;
        options.set_transmit = [](bool) { return Result<void>{}; };
        CHECK_THROWS_AS(SerialLinkChannel::wrap(pair.first, executor, options),
                        std::invalid_argument);
    }
}

TEST_CASE("串行链路写入预算超限被拒绝", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);
    SerialLinkOptions options;
    options.max_pending_writes = 2;
    auto adapter = SerialLinkChannel::wrap(pair.first, executor, options);

    std::optional<Result<void>> rejected;
    const Bytes frame = sample_frame();
    for (int i = 0; i < 4; ++i) {
        adapter->async_write(frame, [&](Result<void> r) {
            if (!static_cast<bool>(r) && !rejected.has_value()) rejected = r;
        });
    }
    executor->run_ready();
    REQUIRE(rejected.has_value());
    CHECK(rejected->error().code == ErrorCode::resource_limit);
    // 排空余下队列，避免析构时留下未交付回调。
    settle(executor);
}

TEST_CASE("串行链路关闭后写入以 closed 结束", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);
    SerialLinkOptions options;
    options.set_transmit = [](bool) { return Result<void>{}; };
    options.async_drain = [](IChannel::WriteHandler callback) { callback(Result<void>{}); };
    auto adapter = SerialLinkChannel::wrap(pair.first, executor, options);

    adapter->close();
    CHECK_NOTHROW(adapter->close());  // 幂等。

    std::optional<Result<void>> outcome;
    const Bytes frame = sample_frame();
    adapter->async_write(frame, [&](Result<void> r) { outcome = std::move(r); });
    executor->run_ready();
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(static_cast<bool>(*outcome));
}

TEST_CASE("串行链路析构不延长底层通道寿命", "[transport][serial_link]") {
    auto executor = std::make_shared<ManualExecutor>();
    std::weak_ptr<MemoryChannel> weak;
    {
        auto pair = MemoryChannel::pair(executor);
        weak = pair.first;
        auto adapter = SerialLinkChannel::wrap(pair.first, executor, SerialLinkOptions{});
        adapter->async_write(sample_frame(), [](Result<void>) {});
        executor->run_ready();
        adapter.reset();
        executor->run_ready();
        // 适配器释放后底层通道仍被 pair 持有。
        CHECK_FALSE(weak.expired());
    }
    CHECK(weak.expired());
    executor->run_ready();
}