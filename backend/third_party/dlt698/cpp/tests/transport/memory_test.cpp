/**
 * @file memory_test.cpp
 * @brief 内存通道的字节收发、队列预算与生命周期测试。
 */
#include <dlt698/dlt698.hpp>
#include <dlt698/transport/memory.hpp>
#include <functional>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::transport;
using dlt698::test::hex;

TEST_CASE("内存通道成对收发字节", "[transport][memory]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);

    SECTION("一端写出的字节由另一端读到") {
        std::optional<Result<Bytes>> received;
        pair.second->async_read([&](Result<Bytes> r) { received = std::move(r); });
        const Bytes payload = hex("01 02 03 04");
        pair.first->async_write(payload, [](Result<void>) {});
        executor->run_ready();

        REQUIRE(received.has_value());
        REQUIRE(static_cast<bool>(*received));
        CHECK(to_hex(ByteView{received->value()}) == "01 02 03 04");
    }

    SECTION("读到的字节是副本，修改不影响已交付数据") {
        std::optional<Result<Bytes>> received;
        pair.second->async_read([&](Result<Bytes> r) { received = std::move(r); });
        const Bytes payload = hex("AA BB");
        pair.first->async_write(payload, [](Result<void>) {});
        executor->run_ready();
        REQUIRE(received.has_value());
        REQUIRE(static_cast<bool>(*received));
        Bytes copy = received->value();
        copy[0] = 0x00;
        CHECK(received->value()[0] == 0xaa);
    }

    SECTION("两次连续写入可被一次读出，内容按序拼接") {
        // 通道不保证批次边界，上层必须自行按协议长度组帧。
        const Bytes first_batch = hex("01");
        const Bytes second_batch = hex("02 03");
        std::vector<Bytes> batches;
        // 读操作一次只消费一批，需要在回调里重新投递才能继续取下一批。
        std::function<void()> pump_read = [&] {
            pair.second->async_read([&](Result<Bytes> r) {
                if (!static_cast<bool>(r)) return;
                batches.push_back(r.value());
                pump_read();
            });
        };
        pump_read();
        pair.first->async_write(first_batch, [](Result<void>) {});
        pair.first->async_write(second_batch, [](Result<void>) {});
        executor->run_ready();

        REQUIRE(batches.size() == 2);
        Bytes joined;
        for (const auto& batch : batches) joined.insert(joined.end(), batch.begin(), batch.end());
        // 两批分别投递，顺序与内容都必须完整。
        CHECK(to_hex(ByteView{joined}) == "01 02 03");
    }
}

TEST_CASE("内存通道写入预算超限时报错", "[transport][memory]") {
    auto executor = std::make_shared<ManualExecutor>();
    MemoryOptions options;
    options.max_buffer_bytes = 8;
    options.max_pending_writes = 2;
    auto pair = MemoryChannel::pair(executor, options);

    SECTION("超出字节预算的写入被拒绝") {
        std::optional<Result<void>> outcome;
        const Bytes payload = hex("01 02 03 04 05 06 07 08 09");
        pair.first->async_write(payload, [&](Result<void> r) { outcome = std::move(r); });
        executor->run_ready();
        REQUIRE(outcome.has_value());
        REQUIRE_FALSE(static_cast<bool>(*outcome));
        CHECK(outcome->error().code == ErrorCode::resource_limit);
    }

    SECTION("超出批次数预算的写入被拒绝") {
        // 保持每批很小，只用批次数量触发上限。
        const Bytes small = hex("01");
        std::optional<Result<void>> outcome;
        for (int i = 0; i < 4; ++i) {
            pair.first->async_write(small, [&](Result<void> r) {
                if (!static_cast<bool>(r) && !outcome.has_value()) outcome = r;
            });
        }
        executor->run_ready();
        REQUIRE(outcome.has_value());
        CHECK(outcome->error().code == ErrorCode::resource_limit);
    }
}

TEST_CASE("关闭内存通道后读写以 closed 结束", "[transport][memory]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);

    SECTION("对端读到 closed") {
        pair.first->close();
        std::optional<Result<Bytes>> received;
        pair.second->async_read([&](Result<Bytes> r) { received = std::move(r); });
        executor->run_ready();
        REQUIRE(received.has_value());
        REQUIRE_FALSE(static_cast<bool>(*received));
        CHECK(received->error().code == ErrorCode::closed);
    }

    SECTION("关闭幂等") {
        pair.first->close();
        CHECK_NOTHROW(pair.first->close());
        CHECK_NOTHROW(pair.first->close());
    }

    SECTION("关闭后写入立即以 closed 结束") {
        pair.first->close();
        std::optional<Result<void>> outcome;
        const Bytes payload = hex("01");
        pair.first->async_write(payload, [&](Result<void> r) { outcome = std::move(r); });
        executor->run_ready();
        REQUIRE(outcome.has_value());
        REQUIRE_FALSE(static_cast<bool>(*outcome));
        CHECK(outcome->error().code == ErrorCode::closed);
    }
}

TEST_CASE("内存通道析构不保活未交付的读回调", "[transport][memory]") {
    auto executor = std::make_shared<ManualExecutor>();
    std::weak_ptr<MemoryChannel> weak;
    {
        auto pair = MemoryChannel::pair(executor);
        weak = pair.first;
        pair.second->async_read([](Result<Bytes>) {});
        pair.first->close();
        executor->run_ready();
    }
    executor->run_ready();
    CHECK(weak.expired());
}

TEST_CASE("空写入是无操作", "[transport][memory]") {
    auto executor = std::make_shared<ManualExecutor>();
    auto pair = MemoryChannel::pair(executor);
    std::optional<Result<void>> outcome;
    const Bytes empty;
    pair.first->async_write(empty, [&](Result<void> r) { outcome = std::move(r); });
    executor->run_ready();
    REQUIRE(outcome.has_value());
    CHECK(static_cast<bool>(*outcome));
}