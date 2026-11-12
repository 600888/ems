/**
 * @file frame_test.cpp
 * @brief 链路帧编解码、CRC 校验与流式拆帧的单元测试。
 */
#include <dlt698/protocol/link/frame.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::protocol::link;
using dlt698::test::fixture;
using dlt698::test::hex;

TEST_CASE("CRC16 符合附录 A 的校验向量", "[link][frame]") {
    // 附录 A：初值 FFFF、反射多项式 8408、输出异或 FFFF，123456789 -> 906E。
    const Bytes check{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16(ByteView{check}) == 0x906e);

    SECTION("空输入得到初值的最终异或结果") {
        const Bytes empty;
        CHECK(crc16(ByteView{empty}) == 0x0000);
    }

    SECTION("单比特翻转改变校验值") {
        const Bytes base = hex("01 02 03 04");
        const std::uint16_t original = crc16(ByteView{base});
        Bytes flipped = base;
        flipped[2] ^= 0x01;
        CHECK(crc16(ByteView{flipped}) != original);
    }
}

TEST_CASE("链路帧按规范向量编解码", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");

    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    const Bytes expected = fixture("get-frame.hex");
    CHECK(to_hex(ByteView{bytes.value()}) == to_hex(ByteView{expected}));
    CHECK(bytes.value().size() == 25);
    CHECK(bytes.value()[1] == 23);  // 长度字段为除起止符外的字节数。

    auto decoded = decode_frame(bytes.value());
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value() == frame);

    SECTION("地址、方向与控制字段完整保留") {
        CHECK(decoded.value().server.type == frame.server.type);
        CHECK(decoded.value().server.bytes == frame.server.bytes);
        CHECK(decoded.value().control == frame.control);
        CHECK(decoded.value().payload == frame.payload);
    }
}

TEST_CASE("帧校验和错误被精确分类", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");
    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    const Bytes original = bytes.value();

    SECTION("校验头错误") {
        Bytes corrupt = original;
        corrupt[12] ^= 0x01;  // 落在头部校验覆盖范围内。
        auto result = decode_frame(corrupt);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::checksum_header);
    }

    SECTION("校验帧错误") {
        Bytes corrupt = original;
        corrupt[16] ^= 0x01;  // 落在帧校验范围内但不在头部。
        auto result = decode_frame(corrupt);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::checksum_frame);
    }

    SECTION("长度字段与实际内容不符") {
        Bytes corrupt = original;
        corrupt[1] = 0xff;
        CHECK_FALSE(static_cast<bool>(decode_frame(corrupt)));
    }

    SECTION("起止符缺失") {
        Bytes corrupt = original;
        corrupt.front() = 0x00;
        CHECK_FALSE(static_cast<bool>(decode_frame(corrupt)));
    }
}

TEST_CASE("流式拆帧器对任意切分点都还原完整帧", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");
    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    const Bytes original = bytes.value();

    SECTION("每个切分点都恰好产出一帧") {
        for (std::size_t split = 0; split <= original.size(); ++split) {
            INFO("切分位置=" << split);
            FrameStreamDecoder decoder;
            auto first = decoder.feed(ByteView{original}.subview(0, split));
            auto second = decoder.feed(ByteView{original}.subview(split, original.size() - split));
            CHECK(first.size() + second.size() == 1);
            const auto& event = first.empty() ? second.front() : first.front();
            REQUIRE(std::holds_alternative<Frame>(event));
            CHECK(std::get<Frame>(event) == frame);
            CHECK(decoder.buffered_size() == 0);
        }
    }

    SECTION("逐字节喂入也能还原") {
        FrameStreamDecoder decoder;
        std::vector<StreamEvent> events;
        for (const auto byte : original) {
            const Bytes single{byte};
            auto produced = decoder.feed(ByteView{single});
            events.insert(events.end(), produced.begin(), produced.end());
        }
        REQUIRE(events.size() == 1);
        CHECK(std::holds_alternative<Frame>(events.front()));
        CHECK(std::get<Frame>(events.front()) == frame);
        CHECK(decoder.buffered_size() == 0);
    }
}

TEST_CASE("流式拆帧器跳过前导填充与损坏帧", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");
    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    Bytes corrupt = bytes.value();
    corrupt[12] ^= 0x01;  // 破坏头部校验。

    // 前导 0xFE 填充、损坏帧、随后两帧完整数据。
    Bytes stream{0xfe, 0xfe, 0x00, 0x68, 0xff, 0xff};
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    stream.insert(stream.end(), bytes.value().begin(), bytes.value().end());
    stream.insert(stream.end(), bytes.value().begin(), bytes.value().end());

    FrameStreamDecoder decoder;
    const auto events = decoder.feed(stream);
    std::size_t frames = 0, errors = 0;
    for (const auto& event : events) {
        if (std::holds_alternative<Frame>(event))
            ++frames;
        else
            ++errors;
    }
    CHECK(frames == 2);  // 损坏帧之后的完整帧仍须交付。
    CHECK(errors >= 1);
    CHECK(decoder.buffered_size() == 0);
}

TEST_CASE("流式拆帧器对纯填充输入不产出事件", "[link][frame]") {
    FrameStreamDecoder decoder;
    const Bytes flood(100000, 0xfe);
    CHECK(decoder.feed(ByteView{flood}).empty());
    CHECK(decoder.buffered_size() == 0);
}

TEST_CASE("帧地址类型覆盖四种取值", "[link][frame]") {
    Frame frame;
    frame.payload = hex("00 68 16 FF");

    SECTION("单地址") {
        frame.server.type = AddressType::single;
        frame.server.bytes = Bytes(16, 0x12);
        frame.server.logical = 3;
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        CHECK(decode_frame(bytes.value()).value() == frame);
    }

    SECTION("通配地址仍须携带至少一个地址字节") {
        // 地址描述字段的低四位编码"地址字节数减一"，零字节无法在帧上表达。
        frame.server.type = AddressType::wildcard;
        frame.server.bytes = {0x00};
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        CHECK(decode_frame(bytes.value()).value() == frame);

        frame.server.bytes.clear();
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
    }

    SECTION("组地址的逻辑地址号上限为 3") {
        frame.server.type = AddressType::group;
        frame.server.bytes = hex("01 02 03");
        frame.server.logical = 3;
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        CHECK(decode_frame(bytes.value()).value() == frame);

        frame.server.logical = 4;
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
    }

    SECTION("广播地址固定为 0xAA") {
        frame.server.type = AddressType::broadcast;
        frame.server.bytes = {0xaa};
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        CHECK(decode_frame(bytes.value()).value() == frame);

        // 广播地址不接受其他取值，这是协议规定的固定字节。
        frame.server.bytes = {0x01};
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
        frame.server.bytes.clear();
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
    }

    SECTION("地址字节数上限为 16") {
        frame.server.type = AddressType::single;
        frame.server.bytes = Bytes(17, 0x01);
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
    }

    SECTION("控制字保留位必须为零，功能码只接受链路管理与用户数据") {
        frame.server.bytes = hex("07 09 19 05 16 20");
        frame.control = 0x10;  // 保留位置位。
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
        frame.control = 0x00;  // 功能码 0 不被接受。
        CHECK(encode_frame(frame).error().code == ErrorCode::invalid_value);
        frame.control = 0x01;
        CHECK(static_cast<bool>(encode_frame(frame)));
    }
}

TEST_CASE("控制字段的 scrambling 位可往返", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");
    frame.control |= 8;  // 置 scrambling 位。

    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    auto decoded = decode_frame(bytes.value());
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value() == frame);
    CHECK((decoded.value().control & 8) != 0);
}

TEST_CASE("帧长度受 14 位字段上限约束", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");

    SECTION("最大负载恰好装满") {
        // 固定开销为11 加地址长度，因此最大负载受二者共同限制。
        frame.payload = Bytes(16385 - (11 + frame.server.bytes.size()), 0);
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        CHECK(bytes.value().size() == 16385);
        CHECK(static_cast<bool>(decode_frame(bytes.value())));
    }

    SECTION("超出上限一字节即被拒绝") {
        frame.payload = Bytes(16385 - (11 + frame.server.bytes.size()) + 1, 0);
        CHECK(encode_frame(frame).error().code == ErrorCode::resource_limit);
    }

    SECTION("可通过更小的 max_frame_bytes 提前拒绝") {
        frame.payload = Bytes(100, 0);
        Limits limits;
        limits.max_frame_bytes = 50;
        CHECK(encode_frame(frame, limits).error().code == ErrorCode::resource_limit);
    }
}

TEST_CASE("帧编解码遵守字节上限", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = Bytes(100, 0x5a);

    SECTION("输出上限通过 max_frame_bytes 生效") {
        Limits limits;
        limits.max_frame_bytes = 32;
        CHECK(encode_frame(frame, limits).error().code == ErrorCode::resource_limit);
    }

    SECTION("输入上限") {
        auto bytes = encode_frame(frame);
        REQUIRE(static_cast<bool>(bytes));
        Limits limits;
        limits.max_frame_bytes = 16;
        CHECK(decode_frame(bytes.value(), limits).error().code == ErrorCode::resource_limit);
    }
}

TEST_CASE("帧解码对每个截断位置都拒绝", "[link][frame]") {
    Frame frame;
    frame.server.bytes = hex("07 09 19 05 16 20");
    frame.payload = fixture("get-normal-request.hex");
    auto bytes = encode_frame(frame);
    REQUIRE(static_cast<bool>(bytes));
    const Bytes original = bytes.value();
    for (std::size_t n = 0; n < original.size(); ++n) {
        INFO("截断到 " << n << " 字节");
        CHECK_FALSE(static_cast<bool>(decode_frame(ByteView{original}.subview(0, n))));
    }
    CHECK(static_cast<bool>(decode_frame(original)));
}