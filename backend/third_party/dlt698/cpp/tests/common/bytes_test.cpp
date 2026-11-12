/**
 * @file bytes_test.cpp
 * @brief 字节视图、读写游标、十六进制转换与资源限制的单元测试。
 */
#include <cstdint>
#include <dlt698/common/bytes.hpp>
#include <limits>
#include <stdexcept>

#include "catch/test_support.hpp"

using namespace dlt698;
using dlt698::test::hex;

TEST_CASE("ByteView 借用内存且禁止借用临时容器", "[common][bytes]") {
    Bytes source = hex("01 02 03 04");

    SECTION("容器构造的视图反映容器当前内容") {
        ByteView view{source};
        CHECK(view.size() == 4);
        CHECK(view.data() == source.data());
        CHECK_FALSE(view.empty());
        CHECK(view[2] == 3);
    }

    SECTION("subview 共享底层内存") {
        ByteView view{source};
        auto tail = view.subview(2, 2);
        CHECK(tail.size() == 2);
        CHECK(tail[0] == source[2]);
        source[2] = 0xab;  // 视图不拥有内存，应立即看到修改。
        CHECK(tail[0] == 0xab);
    }

    SECTION("空区间允许，但越界抛出") {
        ByteView view{source};
        CHECK(view.subview(4, 0).empty());
        CHECK(view.subview(0, 0).empty());
        CHECK_THROWS_AS(view.subview(5, 0), std::out_of_range);
        CHECK_THROWS_AS(view.subview(3, 2), std::out_of_range);
        CHECK_THROWS_AS(view.subview(0, 5), std::out_of_range);
    }

    SECTION("默认视图与空视图") {
        ByteView empty_view;
        CHECK(empty_view.empty());
        CHECK(empty_view.size() == 0);
        CHECK(empty_view.data() == nullptr);
    }

    SECTION("非空视图拒绝空指针") {
        CHECK_THROWS_AS(ByteView(nullptr, 1), std::invalid_argument);
        CHECK_NOTHROW(ByteView(nullptr, 0));
    }

    SECTION("禁止绑定右值容器，避免悬空视图") {
        static_assert(!std::is_constructible_v<ByteView, Bytes&&>);
        static_assert(!std::is_constructible_v<ByteView, const Bytes&&>);
        static_assert(std::is_constructible_v<ByteView, const Bytes&>);
    }
}

TEST_CASE("Reader 按大端序推进游标", "[common][bytes]") {
    Bytes source = hex("01 7F 80 00 FF");

    SECTION("u8 与 be 混合读取") {
        Reader reader{ByteView{source}};
        CHECK(reader.position() == 0);
        CHECK(reader.remaining() == 5);
        CHECK(reader.u8("first") == 1);
        CHECK(reader.remaining() == 4);
        CHECK(reader.be(2, "second") == 0x7f80);
        CHECK(reader.be(1, "third") == 0x00);
        CHECK(reader.be(1, "fourth") == 0xff);
        CHECK(reader.remaining() == 0);
        CHECK_NOTHROW(reader.finish());
    }

    SECTION("be 宽度为零返回零且不推进") {
        Reader reader{ByteView{source}};
        CHECK(reader.be(0, "empty") == 0);
        CHECK(reader.position() == 0);
    }

    SECTION("宽度超过 8 被拒绝") {
        Reader reader{ByteView{source}};
        CHECK_DECODE_ERROR(reader.be(9, "wide"), ErrorCode::invalid_length, "wide");
    }

    SECTION("输入不足时报告 need_more_data 与字段名") {
        // 输入只有 5 字节，请求 8 字节整数必然不足。
        Reader reader{ByteView{source}};
        CHECK_DECODE_ERROR(reader.be(8, "tail"), ErrorCode::need_more_data, "tail");
        // 错误偏移指向已消费到的位置，便于定位截断点。
        try {
            reader.be(8, "tail");
            FAIL("应当抛出 DecodeFailure");
        } catch (const DecodeFailure& failure) {
            CHECK(failure.error.offset == 0);
        }
        // 失败不得推进游标，调用方可以改用更宽的输入重试。
        CHECK(reader.position() == 0);
    }

    SECTION("require 不推进游标") {
        Reader reader{ByteView{source}};
        CHECK_NOTHROW(reader.require(5, "exact"));
        CHECK(reader.position() == 0);
        CHECK_DECODE_ERROR(reader.require(6, "over"), ErrorCode::need_more_data, "over");
        CHECK(reader.position() == 0);
    }

    SECTION("require 在已消费部分后按剩余长度判定") {
        Reader reader{ByteView{source}};
        CHECK(reader.u8() == 1);  // 已消费 1 字节，剩余 4。
        CHECK_DECODE_ERROR(reader.require(5, "rest"), ErrorCode::need_more_data, "rest");
        CHECK_NOTHROW(reader.require(4, "rest"));
        CHECK(reader.position() == 1);
    }

    SECTION("bytes 复制区间并复制内存") {
        Bytes source_copy = source;
        Reader reader{ByteView{source}};
        auto slice = reader.bytes(2, "slice");
        CHECK(slice == Bytes({1, 0x7f}));
        source_copy[0] = 0xee;
        CHECK(slice[0] == 1);
        CHECK(reader.position() == 2);
    }

    SECTION("读取空区间返回空容器") {
        Reader reader{ByteView{source}};
        CHECK(reader.bytes(0, "none").empty());
        CHECK(reader.position() == 0);
    }

    SECTION("存在剩余字节时 finish 报trailing_data") {
        Reader reader{ByteView{source}};
        CHECK(reader.u8() == 1);
        CHECK_DECODE_ERROR(reader.finish(), ErrorCode::trailing_data, "trailing bytes");
    }
}

TEST_CASE("Writer 大端输出并强制资源上限", "[common][bytes]") {
    SECTION("be 按低 n 字节输出，高位被截断") {
        Writer writer{16};
        writer.be(0x1234, 2);
        CHECK(writer.size() == 2);
        writer.be(0xff, 1);
        CHECK(writer.take() == hex("12 34 FF"));
    }

    SECTION("宽度零不输出") {
        Writer writer{4};
        writer.be(0xabcd, 0);
        CHECK(writer.size() == 0);
    }

    SECTION("宽度超过 8 被拒绝") {
        Writer writer{16};
        CHECK_DECODE_ERROR(writer.be(1, 9), ErrorCode::invalid_length, "numeric width");
    }

    SECTION("恰好用满上限时最后一个字节仍可写入") {
        // 上限判断是 n > limit - size，因此输出长度等于上限是合法的。
        Writer writer{3};
        writer.u8(0x11);
        writer.u8(0x22);
        CHECK_NOTHROW(writer.u8(0x33));
        CHECK(writer.take() == hex("11 22 33"));
    }

    SECTION("超出上限的追加被拒绝且不写入数据") {
        Writer writer{3};
        writer.u8(0x11);
        writer.u8(0x22);
        writer.u8(0x33);  // 已用满 3 字节。
        CHECK_DECODE_ERROR(writer.u8(0x44), ErrorCode::resource_limit, "output limit");
        CHECK(writer.take() == hex("11 22 33"));
    }

    SECTION("bytes 追加视图内容") {
        Writer writer{8};
        const Bytes payload = hex("AA BB");
        writer.bytes(ByteView{payload});
        writer.bytes(ByteView{});  // 空视图不消耗配额。
        CHECK(writer.take() == hex("AA BB"));
    }

    SECTION("超限的批量追加也被拒绝") {
        Writer writer{2};
        const Bytes payload = hex("01 02 03");
        CHECK_DECODE_ERROR(writer.bytes(ByteView{payload}), ErrorCode::resource_limit,
                           "output limit");
    }
}

TEST_CASE("from_hex 与 to_hex 互逆", "[common][bytes]") {
    SECTION("空白与大小写混排可解析") {
        auto value = from_hex("  01 02\n03\t04 ");
        REQUIRE(value);
        CHECK(value.value() == hex("01 02 03 04"));
    }

    SECTION("空文本得到空序列") {
        auto value = from_hex("");
        REQUIRE(value);
        CHECK(value.value().empty());
        CHECK(to_hex(ByteView{}).empty());
    }

    SECTION("非法字符报invalid_value 并给出文本偏移") {
        auto value = from_hex("01 0G");
        REQUIRE_FALSE(static_cast<bool>(value));
        CHECK(value.error().code == ErrorCode::invalid_value);
        CHECK(value.error().offset == 4);  // 'G' 的文本下标。
    }

    SECTION("不接受 0x 前缀") {
        auto value = from_hex("0x01");
        REQUIRE_FALSE(static_cast<bool>(value));
        CHECK(value.error().code == ErrorCode::invalid_value);
    }

    SECTION("奇数位报 invalid_length") {
        auto value = from_hex("012");
        REQUIRE_FALSE(static_cast<bool>(value));
        CHECK(value.error().code == ErrorCode::invalid_length);
    }

    SECTION("半字节之间的空白也判为非法") {
        auto value = from_hex("0 1");
        REQUIRE_FALSE(static_cast<bool>(value));
        CHECK(value.error().code == ErrorCode::invalid_value);
    }

    SECTION("to_hex 输出大写且以单空格分隔") {
        const Bytes bytes = hex("de ad be ef");
        CHECK(to_hex(ByteView{bytes}) == "DE AD BE EF");
    }

    SECTION("大宽度整数往返保持位模式") {
        const std::uint64_t value = std::numeric_limits<std::uint64_t>::max();
        Writer writer{8};
        writer.be(value, 8);
        Bytes encoded = writer.take();
        Reader reader{ByteView{encoded}};
        CHECK(reader.be(8, "wide") == value);
        CHECK(to_hex(ByteView{encoded}) == "FF FF FF FF FF FF FF FF");
    }
}

TEST_CASE("Limits 的默认值覆盖最大帧与流缓冲", "[common][bytes]") {
    const Limits limits;
    // 14 位长度字段最多16383 字节，加上起止符即 max_frame_bytes。
    CHECK(limits.max_frame_bytes == 16385);
    CHECK(limits.max_stream_bytes > limits.max_frame_bytes);
    CHECK(limits.max_data_bytes >= limits.max_frame_bytes);
    CHECK(limits.max_elements == 65536);
    CHECK(limits.max_depth == 32);
}