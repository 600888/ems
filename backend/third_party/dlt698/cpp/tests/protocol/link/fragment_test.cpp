/**
 * @file fragment_test.cpp
 * @brief 链路分片编解码、分片状态机与重组器的单元测试。
 */
#include <dlt698/protocol/link/fragment.hpp>
#include <stdexcept>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::protocol::link;
using dlt698::test::hex;

namespace {

/**
 * @brief 断言分片解码失败，并同时比对错误分类与上下文字段。
 * @param[in] text 十六进制输入文本。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 * @note 共享辅助只比对错误码，链路分片的错误上下文需要一并核对。
 */
void require_fragment_error(const char* text, ErrorCode code, const char* context) {
    INFO("输入=" << text);
    const Bytes bytes = hex(text);
    auto result = decode_fragment(ByteView{bytes});
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

}  // namespace

/**
 * @brief 校验给定分片的编码字节与模型一致，并验证解码后逐字段相同。
 * @param[in] expected 期望的十六进制文本。
 * @param[in] fragment 待校验的片段模型。
 * @note 用十六进制文本而非字节容器比较，失败信息才能直接看出差异位置。
 */
void expect_fragment_encoding(const char* expected, const Fragment& fragment) {
    INFO("期望字节=" << expected);
    auto encoded = encode_fragment(fragment);
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();
    const Bytes expected_bytes = hex(expected);
    CHECK(to_hex(ByteView{bytes}) == to_hex(ByteView{expected_bytes}));

    auto decoded = decode_fragment(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value().type == fragment.type);
    CHECK(decoded.value().sequence == fragment.sequence);
    const Bytes data = decoded.value().data;
    const Bytes expected_data = fragment.data;
    CHECK(to_hex(ByteView{data}) == to_hex(ByteView{expected_data}));

    // 解码结果必须能再次编码得到同一串字节。
    auto re_encoded = encode_fragment(decoded.value());
    REQUIRE(static_cast<bool>(re_encoded));
    const Bytes result = re_encoded.value();
    CHECK(to_hex(ByteView{result}) == to_hex(ByteView{expected_bytes}));
}

TEST_CASE("链路分片按序推进", "[link][fragment]") {
    // 九字节 APDU 按三字节一片切成 first/middle/last 三片。
    const Bytes apdu = hex("00 01 02 03 04 05 06 07 08");
    LinkFragmenter fragmenter{apdu, 3, 1024};

    SECTION("首片类型为 first 且序号从零开始") {
        const Fragment current = fragmenter.current();
        CHECK(current.type == FragmentType::first);
        CHECK(current.sequence == 0);
        CHECK(current.data == hex("00 01 02"));
    }

    SECTION("current 重复调用不前进，可用于超时重发") {
        const Fragment first = fragmenter.current();
        const Fragment again = fragmenter.current();
        CHECK(again.type == first.type);
        CHECK(again.sequence == first.sequence);
        CHECK(again.data == first.data);
        // 只有确认才推进；在此之前反复读取始终得到同一片。
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(first.sequence)));
        const Fragment second = fragmenter.current();
        CHECK(second.sequence == first.sequence + 1);
        // 确认已经推进，重复读取得到的仍是新的一片。
        CHECK(fragmenter.current().data == second.data);
    }

    SECTION("每次确认后推进到下一片并递增序号") {
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        const Fragment second = fragmenter.current();
        CHECK(second.type == FragmentType::middle);
        CHECK(second.sequence == 1);
        CHECK(second.data == hex("03 04 05"));

        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        const Fragment third = fragmenter.current();
        CHECK(third.type == FragmentType::last);
        CHECK(third.sequence == 2);
        CHECK(third.data == hex("06 07 08"));
    }

    SECTION("发送进度完全由确认驱动") {
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        CHECK(fragmenter.current().sequence == 1);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        CHECK(fragmenter.current().sequence == 2);
        // 末片交付应用，不再等待链路确认，因此确认末片被拒绝。
        CHECK(fragmenter.acknowledge(2).error().code == ErrorCode::invalid_value);
    }

    SECTION("分片器与重组器可以配合还原原始 APDU") {
        LinkReassembler reassembler{64};
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        // 末片直接交付，不需要确认。
        const Bytes first = hex("00 01 02");
        const Bytes second = hex("03 04 05");
        const Bytes third = hex("06 07 08");
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::middle, 1, second})));
        auto last = reassembler.accept(Fragment{FragmentType::last, 2, third});
        REQUIRE(static_cast<bool>(last));
        REQUIRE(last.value().apdu.has_value());
        const Bytes restored = last.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == to_hex(ByteView{apdu}));
    }
}

TEST_CASE("分片构造参数非法时抛出", "[link][fragment]") {
    SECTION("片大小为零不可接受") {
        CHECK_THROWS_AS(LinkFragmenter(hex("00 01 02 03"), 0, 1024), std::invalid_argument);
    }

    SECTION("APDU 不超过一片时无法产生多片序列") {
        // 四字节配四字节片只得到一片，构造阶段即拒绝。
        CHECK_THROWS_AS(LinkFragmenter(hex("00 01 02 03"), 4, 1024), std::invalid_argument);
        CHECK_THROWS_AS(LinkFragmenter(hex("00 01 02 03"), 5, 1024), std::invalid_argument);
    }

    SECTION("APDU 超过拥有内存上限被拒绝") {
        CHECK_THROWS_AS(LinkFragmenter(hex("00 01 02 03 04"), 2, 4), std::invalid_argument);
    }

    SECTION("恰好用满上限合法") { CHECK_NOTHROW(LinkFragmenter(hex("00 01 02 03 04"), 2, 5)); }

    SECTION("重组预算为零不可接受") {
        CHECK_THROWS_AS(LinkReassembler(0), std::invalid_argument);
        CHECK_NOTHROW(LinkReassembler(1));
    }
}

TEST_CASE("acknowledge 拒绝过期或超前序号", "[link][fragment]") {
    const Bytes apdu = hex("00 01 02 03 04 05 06 07 08");
    LinkFragmenter fragmenter{apdu, 3, 1024};

    SECTION("超前序号被拒绝且不推进") {
        CHECK(fragmenter.acknowledge(1).error().code == ErrorCode::invalid_value);
        CHECK(fragmenter.current().sequence == 0);
    }

    SECTION("已确认过的过期序号被拒绝") {
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        CHECK(fragmenter.acknowledge(0).error().code == ErrorCode::invalid_value);
        // 被拒绝的确认不得改变发送位置。
        CHECK(fragmenter.current().sequence == 1);
    }

    SECTION("序号只前进不回退") {
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        CHECK(fragmenter.acknowledge(2).error().code == ErrorCode::invalid_value);
        CHECK(fragmenter.current().sequence == 1);
    }
}

TEST_CASE("分片器覆盖两片与整除边界", "[link][fragment]") {
    SECTION("恰好两片时第二片即为末片") {
        // 七字节配三字节片：3+3+1，中间片仍然出现。
        LinkFragmenter fragmenter{hex("00 01 02 03 04 05 06"), 3, 1024};
        CHECK(fragmenter.current().type == FragmentType::first);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        const Fragment second = fragmenter.current();
        CHECK(second.type == FragmentType::middle);
        CHECK(second.sequence == 1);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        const Fragment third = fragmenter.current();
        CHECK(third.type == FragmentType::last);
        CHECK(third.data == hex("06"));
    }

    SECTION("六字节配两字节片得到三片") {
        LinkFragmenter fragmenter{Bytes(6, 0x5a), 2, 1024};
        CHECK(fragmenter.current().type == FragmentType::first);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        CHECK(fragmenter.current().type == FragmentType::middle);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        const Fragment third = fragmenter.current();
        CHECK(third.type == FragmentType::last);
        CHECK(third.sequence == 2);
        CHECK(third.data.size() == 2);
    }

    SECTION("末片确认被拒绝后仍停在末片") {
        LinkFragmenter fragmenter{Bytes(6, 0x5a), 2, 1024};
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(0)));
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(1)));
        CHECK(fragmenter.acknowledge(2).error().code == ErrorCode::invalid_value);
        CHECK(fragmenter.current().type == FragmentType::last);
    }
}

TEST_CASE("分片格式域按小端编码", "[link][fragment]") {
    SECTION("四种 FragmentType 往返") {
        expect_fragment_encoding("BC 0A AA BB", Fragment{FragmentType::first, 0xabc, hex("AA BB")});
        expect_fragment_encoding("BC 4A AA BB", Fragment{FragmentType::last, 0xabc, hex("AA BB")});
        expect_fragment_encoding("BC CA AA BB",
                                 Fragment{FragmentType::middle, 0xabc, hex("AA BB")});
        // 确认帧只有两字节格式域，不带任何数据。
        expect_fragment_encoding("BC 8A", Fragment{FragmentType::acknowledgement, 0xabc, {}});
    }

    SECTION("序号覆盖 0 与 12 位上限") {
        expect_fragment_encoding("00 00 01", Fragment{FragmentType::first, 0, hex("01")});
        expect_fragment_encoding("FF 0F 01", Fragment{FragmentType::first, 4095, hex("01")});
        expect_fragment_encoding("FF 8F", Fragment{FragmentType::acknowledgement, 4095, {}});
        // 序号只占低 12 位，4092 的低字节为 FC、高字节低四位为 8。
        expect_fragment_encoding("FC 8F", Fragment{FragmentType::acknowledgement, 4092, {}});
    }

    SECTION("数据长度只影响两字节格式域之后的负载") {
        for (std::size_t n = 1; n <= 4; ++n) {
            INFO("数据字节数=" << n);
            auto encoded = encode_fragment(Fragment{FragmentType::middle, 1, Bytes(n, 0xa5)});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            CHECK(bytes.size() == n + 2);
        }
    }

    SECTION("解码后可再编码得到相同字节") {
        const Bytes bytes = hex("BC CA 01 02 03");
        auto decoded = decode_fragment(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        auto re_encoded = encode_fragment(decoded.value());
        REQUIRE(static_cast<bool>(re_encoded));
        const Bytes result = re_encoded.value();
        CHECK(to_hex(ByteView{result}) == to_hex(ByteView{bytes}));
    }
}

TEST_CASE("分片格式域保留位与类型被校验", "[link][fragment]") {
    SECTION("序号超过 12 位被拒绝") {
        Fragment fragment{FragmentType::first, 4096, hex("01")};
        auto encoded = encode_fragment(fragment);
        CHECK(encoded.error().code == ErrorCode::invalid_value);
        CHECK(encoded.error().context == "fragment fields");
    }

    SECTION("类型取值越界被拒绝") {
        // 类型由调用方给出，越界值必须由编码器挡住而不是截断到低两位。
        Fragment fragment{static_cast<FragmentType>(4), 0, hex("01")};
        CHECK(encode_fragment(fragment).error().code == ErrorCode::invalid_value);
        Fragment high{static_cast<FragmentType>(0xff), 0, hex("01")};
        CHECK(encode_fragment(high).error().code == ErrorCode::invalid_value);
    }

    SECTION("格式域bit12 与 bit13 是保留位") {
        // 0x1000 与 0x2000 只置位保留位，高两位仍为零，因此报保留位而非类型错误。
        require_fragment_error("00 10", ErrorCode::invalid_value, "fragment reserved bits");
        require_fragment_error("00 20", ErrorCode::invalid_value, "fragment reserved bits");
    }

    SECTION("确认帧不得携带数据") {
        Fragment fragment{FragmentType::acknowledgement, 1, hex("01")};
        auto encoded = encode_fragment(fragment);
        CHECK(encoded.error().code == ErrorCode::invalid_length);
        CHECK(encoded.error().context == "fragment data");
        // 反向解码同样拒绝带数据的确认帧：格式域 0x8000 小端写成 00 80。
        require_fragment_error("00 80 AA", ErrorCode::invalid_length, "fragment data");
        // 同理确认帧不能是空数据帧，缺数据时报长度错误。
        require_fragment_error("80 00", ErrorCode::invalid_length, "fragment data");
    }

    SECTION("数据帧不得为空") {
        CHECK(encode_fragment(Fragment{FragmentType::first, 1, {}}).error().code ==
              ErrorCode::invalid_length);
        // middle 与 last 同样必须携带数据。
        CHECK(encode_fragment(Fragment{FragmentType::middle, 1, {}}).error().code ==
              ErrorCode::invalid_length);
        CHECK(encode_fragment(Fragment{FragmentType::last, 1, {}}).error().code ==
              ErrorCode::invalid_length);
        require_fragment_error("C0 00", ErrorCode::invalid_length, "fragment data");
        require_fragment_error("40 00", ErrorCode::invalid_length, "fragment data");
    }

    SECTION("不足两个字节无法读出格式域") {
        require_fragment_error("", ErrorCode::need_more_data, "fragment header");
        require_fragment_error("00", ErrorCode::need_more_data, "fragment header");
    }
}

TEST_CASE("重组器按序拼接并只在末片交付", "[link][fragment]") {
    LinkReassembler reassembler{64};
    const Bytes first = hex("00 01");
    const Bytes second = hex("02 03");
    const Bytes third = hex("04 05");

    SECTION("起始片之后进入等待状态并给出待确认序号") {
        CHECK_FALSE(reassembler.active());
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, first});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().acknowledge.has_value());
        CHECK(result.value().acknowledge.value() == 0);
        CHECK_FALSE(result.value().apdu.has_value());
        CHECK_FALSE(result.value().duplicate);
        CHECK(reassembler.active());
    }

    SECTION("中间片继续等待且不交付 APDU") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        auto result = reassembler.accept(Fragment{FragmentType::middle, 1, second});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().acknowledge.value() == 1);
        CHECK_FALSE(result.value().apdu.has_value());
        CHECK(reassembler.active());
    }

    SECTION("末片直接交付完整 APDU 且不再等待") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::middle, 1, second})));
        auto result = reassembler.accept(Fragment{FragmentType::last, 2, third});
        REQUIRE(static_cast<bool>(result));
        // 末片直接交付应用，不等待链路确认。
        CHECK_FALSE(result.value().acknowledge.has_value());
        REQUIRE(result.value().apdu.has_value());
        const Bytes restored = result.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == "00 01 02 03 04 05");
        CHECK_FALSE(reassembler.active());
    }

    SECTION("first 与 last 两片即可完成一次重组") {
        LinkReassembler pair{64};
        REQUIRE(static_cast<bool>(pair.accept(Fragment{FragmentType::first, 0, first})));
        auto result = pair.accept(Fragment{FragmentType::last, 1, third});
        REQUIRE(static_cast<bool>(result));
        REQUIRE(result.value().apdu.has_value());
        const Bytes restored = result.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == "00 01 04 05");
        CHECK_FALSE(pair.active());
    }

    SECTION("完成后可以立即开始下一次重组") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::last, 1, third})));
        const Bytes restarted = hex("AA");
        auto again = reassembler.accept(Fragment{FragmentType::first, 0, restarted});
        REQUIRE(static_cast<bool>(again));
        CHECK(reassembler.active());
    }
}

TEST_CASE("重组器拒绝乱序与非法起始", "[link][fragment]") {
    LinkReassembler reassembler{64};
    REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, hex("00 01")})));

    SECTION("跳号不污染已有缓冲") {
        auto result = reassembler.accept(Fragment{FragmentType::middle, 5, hex("02")});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "out of order fragment");
        // 乱序不得破坏已有进度，随后仍能按序完成。
        CHECK(reassembler.active());
        auto last = reassembler.accept(Fragment{FragmentType::last, 1, hex("02 03")});
        REQUIRE(static_cast<bool>(last));
        REQUIRE(last.value().apdu.has_value());
        const Bytes restored = last.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == "00 01 02 03");
    }

    SECTION("序号回退被拒绝") {
        REQUIRE(
            static_cast<bool>(reassembler.accept(Fragment{FragmentType::middle, 1, hex("02")})));
        auto result = reassembler.accept(Fragment{FragmentType::middle, 0, hex("03")});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "out of order fragment");
    }

    SECTION("重复首片序号时按不同数据处理并被拒绝") {
        // 序号相同但内容不同，不是重复投递，而是试图重启一条正在进行的重组。
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, hex("AA BB")});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "unexpected first fragment");
    }

    SECTION("起始片序号必须为零") {
        LinkReassembler fresh{64};
        auto result = fresh.accept(Fragment{FragmentType::first, 3, hex("00")});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "unexpected first fragment");
    }

    SECTION("没有起始片时中间片与末片都被拒绝") {
        LinkReassembler fresh{64};
        auto middle = fresh.accept(Fragment{FragmentType::middle, 0, hex("00")});
        CHECK(middle.error().code == ErrorCode::invalid_value);
        CHECK(middle.error().context == "out of order fragment");
        auto last = fresh.accept(Fragment{FragmentType::last, 1, hex("00")});
        CHECK(last.error().code == ErrorCode::invalid_value);
        CHECK_FALSE(fresh.active());
    }
}

TEST_CASE("重复片段不重复提交 APDU", "[link][fragment]") {
    LinkReassembler reassembler{64};
    const Bytes first = hex("00 01");
    REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));

    SECTION("重复首片仍给出确认序号以便重发") {
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, first});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().duplicate);
        // 重发场景下必须继续告知对端已收到哪一片，否则链路会一直重传。
        REQUIRE(result.value().acknowledge.has_value());
        CHECK(result.value().acknowledge.value() == 0);
        CHECK_FALSE(result.value().apdu.has_value());
    }

    SECTION("重复中间片被标记但不推进") {
        REQUIRE(
            static_cast<bool>(reassembler.accept(Fragment{FragmentType::middle, 1, hex("02")})));
        auto result = reassembler.accept(Fragment{FragmentType::middle, 1, hex("02")});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().duplicate);
        CHECK(result.value().acknowledge.value() == 1);
        // 缓冲未被重复追加。
        auto last = reassembler.accept(Fragment{FragmentType::last, 2, hex("03")});
        REQUIRE(static_cast<bool>(last));
        REQUIRE(last.value().apdu.has_value());
        const Bytes restored = last.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == "00 01 02 03");
    }

    SECTION("重复末片不再交付第二个 APDU") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::last, 1, hex("02")})));
        auto result = reassembler.accept(Fragment{FragmentType::last, 1, hex("02")});
        REQUIRE(static_cast<bool>(result));
        CHECK(result.value().duplicate);
        // 已交付过的 APDU 不得再次提交给应用。
        CHECK_FALSE(result.value().apdu.has_value());
        CHECK_FALSE(result.value().acknowledge.has_value());
    }

    SECTION("内容不同的同序号片段不算重复") {
        auto result = reassembler.accept(Fragment{FragmentType::middle, 0, hex("02")});
        // 序号与类型都不同（first 对 middle），按新片处理并因不是期望序号而失败。
        CHECK_FALSE(static_cast<bool>(result));
    }
}

TEST_CASE("重组器拒绝确认帧与空数据", "[link][fragment]") {
    LinkReassembler reassembler{64};

    SECTION("确认帧不是重组输入") {
        auto result = reassembler.accept(Fragment{FragmentType::acknowledgement, 0, {}});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "reassembly fragment");
    }

    SECTION("带数据的确认帧同样被拒绝") {
        auto result = reassembler.accept(Fragment{FragmentType::acknowledgement, 0, hex("01")});
        CHECK(result.error().code == ErrorCode::invalid_value);
    }

    SECTION("空数据片段被拒绝") {
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, {}});
        CHECK(result.error().code == ErrorCode::invalid_value);
        CHECK(result.error().context == "reassembly fragment");
        CHECK_FALSE(reassembler.active());
    }

    SECTION("序号与类型越界同样被拒绝") {
        auto high = reassembler.accept(Fragment{FragmentType::first, 4096, hex("01")});
        CHECK(high.error().code == ErrorCode::invalid_value);
        auto bad_type = reassembler.accept(Fragment{static_cast<FragmentType>(7), 0, hex("01")});
        CHECK(bad_type.error().code == ErrorCode::invalid_value);
    }
}

TEST_CASE("完整 APDU 受重组字节上限约束", "[link][fragment]") {
    SECTION("恰好用满上限时正常交付") {
        LinkReassembler reassembler{5};
        REQUIRE(static_cast<bool>(
            reassembler.accept(Fragment{FragmentType::first, 0, hex("00 01 02")})));
        auto result = reassembler.accept(Fragment{FragmentType::last, 1, hex("03 04")});
        REQUIRE(static_cast<bool>(result));
        REQUIRE(result.value().apdu.has_value());
        const Bytes restored = result.value().apdu.value();
        CHECK(restored.size() == 5);
    }

    SECTION("超出一字节即被拒绝并清理缓冲") {
        LinkReassembler reassembler{4};
        REQUIRE(static_cast<bool>(
            reassembler.accept(Fragment{FragmentType::first, 0, hex("00 01 02")})));
        auto result = reassembler.accept(Fragment{FragmentType::last, 1, hex("03 04 05")});
        CHECK(result.error().code == ErrorCode::resource_limit);
        CHECK(result.error().context == "reassembled APDU bytes");
        // 超限后不得留下半个重组等待后续片段。
        CHECK_FALSE(reassembler.active());
    }

    SECTION("单片本身就超过上限时被拒绝") {
        LinkReassembler reassembler{2};
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, hex("00 01 02")});
        CHECK(result.error().code == ErrorCode::resource_limit);
        CHECK_FALSE(reassembler.active());
    }
}

TEST_CASE("reset 终止重组并清除重复记忆", "[link][fragment]") {
    LinkReassembler reassembler{64};
    const Bytes first = hex("00 01");

    SECTION("reset 后不再处于等待状态") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        CHECK(reassembler.active());
        reassembler.reset();
        CHECK_FALSE(reassembler.active());
    }

    SECTION("reset 丢弃已缓冲的字节") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        REQUIRE(
            static_cast<bool>(reassembler.accept(Fragment{FragmentType::middle, 1, hex("02")})));
        reassembler.reset();
        // 期望序号回到零，缓冲清空，因此序号 2 的末片不再是连续片。
        auto stale = reassembler.accept(Fragment{FragmentType::last, 2, hex("03")});
        CHECK(stale.error().code == ErrorCode::invalid_value);
        // 以新的首片重新开始时不会与旧的重复记忆混淆。
        const Bytes restarted = hex("AA BB CC");
        auto again = reassembler.accept(Fragment{FragmentType::first, 0, restarted});
        REQUIRE(static_cast<bool>(again));
        CHECK_FALSE(again.value().duplicate);
        auto last = reassembler.accept(Fragment{FragmentType::last, 1, hex("DD")});
        REQUIRE(static_cast<bool>(last));
        REQUIRE(last.value().apdu.has_value());
        const Bytes restored = last.value().apdu.value();
        CHECK(to_hex(ByteView{restored}) == "AA BB CC DD");
    }

    SECTION("对未开始的重组调用 reset 是安全的空操作") {
        CHECK_NOTHROW(reassembler.reset());
        CHECK_FALSE(reassembler.active());
    }

    SECTION("reset 之后重复记忆不复用") {
        REQUIRE(static_cast<bool>(reassembler.accept(Fragment{FragmentType::first, 0, first})));
        reassembler.reset();
        // 同样的首片在 reset 之后属于全新一条重组，不是重复投递。
        auto result = reassembler.accept(Fragment{FragmentType::first, 0, first});
        REQUIRE(static_cast<bool>(result));
        CHECK_FALSE(result.value().duplicate);
    }
}

TEST_CASE("真实 APDU 按链路分片往返", "[link][fragment]") {
    // 用 GET-Response 向量作为负载，确认分片层对实际 APDU 长度透明。
    const Bytes apdu = hex("85 01 01 40 01 02 00 01 09 06 12 34 56 78 90 12 00 00");
    LinkFragmenter fragmenter{apdu, 6, 1024};
    LinkReassembler reassembler{1024};

    std::vector<Fragment> produced;
    for (;;) {
        const Fragment current = fragmenter.current();
        produced.push_back(current);
        auto result = reassembler.accept(current);
        REQUIRE(static_cast<bool>(result));
        if (current.type == FragmentType::last) {
            REQUIRE(result.value().apdu.has_value());
            const Bytes restored = result.value().apdu.value();
            CHECK(to_hex(ByteView{restored}) == to_hex(ByteView{apdu}));
            break;
        }
        REQUIRE(result.value().acknowledge.value() == current.sequence);
        REQUIRE(static_cast<bool>(fragmenter.acknowledge(current.sequence)));
    }

    SECTION("十八字节负载按六字节片切成三片") {
        REQUIRE(produced.size() == 3);
        CHECK(produced[0].type == FragmentType::first);
        CHECK(produced[1].type == FragmentType::middle);
        CHECK(produced[2].type == FragmentType::last);
        for (std::size_t i = 0; i < produced.size(); ++i) {
            INFO("第 " << i << " 片");
            CHECK(produced[i].sequence == i);
        }
    }

    SECTION("每片都能独立编解码且片长等于预算") {
        for (const auto& fragment : produced) {
            INFO("序号=" << fragment.sequence);
            auto encoded = encode_fragment(fragment);
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            // 两字节格式域加数据。
            CHECK(bytes.size() == fragment.data.size() + 2);
            auto decoded = decode_fragment(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(decoded.value().data == fragment.data);
        }
    }
}