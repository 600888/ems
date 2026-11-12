/**
 * @file data_codec_test.cpp
 * @brief Data 编解码的单元测试：标签往返、边界检查与资源限制。
 */
#include <cstdint>
#include <dlt698/codec/data_codec.hpp>
#include <dlt698/dlt698.hpp>
#include <limits>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using dlt698::test::hex;
using dlt698::test::require_decode_error;

namespace {

/**
 * @brief 校验给定 Data 的编码字节与模型完全一致。
 * @param[in] expected 期望的十六进制文本。
 * @param[in] value 待校验的数据模型。
 * @note 用十六进制文本而非字节容器比较，失败信息才能直接看出差异位置。
 */
void expect_encoding(const char* expected, const Data& value) {
    const Bytes expected_bytes = hex(expected);
    auto encoded = codec::encode_data(value);
    REQUIRE(static_cast<bool>(encoded));
    CHECK(to_hex(ByteView{encoded.value()}) == to_hex(ByteView{expected_bytes}));
    auto decoded = codec::decode_data(expected_bytes);
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value() == value);
    CHECK(decoded.value().type() == value.type());
    auto re_encoded = codec::encode_data(decoded.value());
    REQUIRE(static_cast<bool>(re_encoded));
    CHECK(to_hex(ByteView{re_encoded.value()}) == to_hex(ByteView{expected_bytes}));
}

}  // namespace

TEST_CASE("Data 基本类型按标签往返", "[codec][data]") {
    expect_encoding("00", Null{});
    expect_encoding("03 01", Boolean{true});
    expect_encoding("03 00", Boolean{false});
    expect_encoding("0F 80", Int8{-128});
    expect_encoding("0F 7F", Int8{127});
    expect_encoding("10 80 00", Int16{-32768});
    expect_encoding("10 7F FF", Int16{32767});
    expect_encoding("05 80 00 00 00", Int32{std::numeric_limits<std::int32_t>::min()});
    expect_encoding("05 7F FF FF FF", Int32{std::numeric_limits<std::int32_t>::max()});
    expect_encoding("14 80 00 00 00 00 00 00 00", Int64{std::numeric_limits<std::int64_t>::min()});
    expect_encoding("11 FF", UInt8{255});
    expect_encoding("12 FF FF", UInt16{65535});
    expect_encoding("06 FF FF FF FF", UInt32{std::numeric_limits<std::uint32_t>::max()});
    expect_encoding("15 FF FF FF FF FF FF FF FF",
                    UInt64{std::numeric_limits<std::uint64_t>::max()});
    expect_encoding("16 02", Enum{2});
    expect_encoding("17 3F 80 00 00", Float32{1.0f});
    expect_encoding("18 C0 04 00 00 00 00 00 00", Float64{-2.5});
    expect_encoding("09 03 00 68 16", OctetString{hex("00 68 16")});
    expect_encoding("0A 03 41 42 43", VisibleString{"ABC"});
    expect_encoding("09 00", OctetString{Bytes{}});
    expect_encoding("0A 00", VisibleString{""});
    expect_encoding("09 01 FF", OctetString{Bytes{0xff}});
}

TEST_CASE("Data 字符串按 UTF-8 字节长度封装", "[codec][data]") {
    expect_encoding("0C 03 E4 B8 AD", Utf8String{std::string("\xE4\xB8\xAD")});
    expect_encoding("0C 00", Utf8String{""});

    SECTION("多字节序列按字节数而非字符数计算长度") {
        const std::string two_chars = "\xE4\xB8\xAD\xE6\x96\x87";  //两个汉字共六字节。
        const Data value = Utf8String{two_chars};
        auto encoded = codec::encode_data(value);
        REQUIRE(encoded);
        CHECK(encoded.value()[1] == 6);
    }
}

TEST_CASE("Data 日期时间保留未指定标记", "[codec][data]") {
    // 日期时间不附加时区，也不校验日历合法性，FF/FFFF 表示未指定。
    expect_encoding("1C 07 E0 01 14 FF FF FF", DateTimeS{{0x07, 0xe0, 1, 20, 0xff, 0xff, 0xff}});
    expect_encoding("19 07 E0 01 14 03 00 00 00 00 64",
                    DateTime{{0x07, 0xe0, 1, 20, 3, 0, 0, 0, 0, 100}});
    expect_encoding("1A FF FF FF FF FF", Date{{0xff, 0xff, 0xff, 0xff, 0xff}});
    expect_encoding("1B 17 3B 3B", Time{{23, 59, 59}});
    expect_encoding("1B 00 00 00", Time{{0, 0, 0}});
}

TEST_CASE("Data 描述符类型按大端字段编码", "[codec][data]") {
    expect_encoding("50 40 01", Oi{0x4001});
    expect_encoding("51 40 01 02 00", Oad{0x4001, 2, 0});

    expect_encoding("53 40 00 01 00", Omd{0x4000, 1, 0});
    expect_encoding("54 00 00 1E", Ti{0, 30});
    expect_encoding("55 06 04 00 00 00 01 21", Tsa{hex("04 00 00 00 01 21")});
    expect_encoding("59 FF 21", ScalerUnit{-1, 33});

    SECTION("OAD 保留属性特征位，不做掩码") {
        auto encoded = codec::encode_data(Oad{0x4001, 0x82, 0x03});  // N=1 索引=2
        REQUIRE(encoded);
        CHECK(encoded.value() == hex("51 40 01 82 03"));
        auto decoded = codec::decode_data(encoded.value());
        REQUIRE(decoded);
        const auto& oad = std::get<Oad>(decoded.value().payload);
        CHECK(oad.attribute == 0x82);  // 特征位属于线上字节，不能被清零。
        CHECK(oad.index == 3);
    }
}

TEST_CASE("Data 复合类型保留顺序与精确元素类型", "[codec][data]") {
    expect_encoding("01 02 12 09 6D 12 09 6D", Array{{UInt16{2413}, UInt16{2413}}});
    expect_encoding("02 02 11 01 01 01 00", Structure{{UInt8{1}, Array{{Null{}}}}});
    expect_encoding("01 00", Array{});
    expect_encoding("02 00", Structure{});

    SECTION("array 与 structure 标签不同但结构可同形") {
        const Bytes array_bytes = hex("01 01 11 05");
        const Bytes structure_bytes = hex("02 01 11 05");
        auto array = codec::decode_data(array_bytes);
        auto structure = codec::decode_data(structure_bytes);
        REQUIRE(array);
        REQUIRE(structure);
        CHECK(array.value().type() == DataType::array);
        CHECK(structure.value().type() == DataType::structure);
        // 同形数据再编码后不应互相转换。
        CHECK(codec::encode_data(array.value()).value() == hex("01 01 11 05"));
        CHECK(codec::encode_data(structure.value()).value() == hex("02 01 11 05"));
    }

    SECTION("嵌套深度按结构保留") {
        const Data nested = Array{{Structure{{Array{{UInt32{7}}}}}}};
        auto encoded = codec::encode_data(nested);
        REQUIRE(encoded);
        auto decoded = codec::decode_data(encoded.value());
        REQUIRE(decoded);
        CHECK(decoded.value() == nested);
    }
}

TEST_CASE("Data 位串校验有效位与填充位", "[codec][data]") {
    expect_encoding("04 09 80 80", BitString{9, hex("80 80")});
    expect_encoding("04 00", BitString{0, Bytes{}});
    expect_encoding("04 01 80", BitString{1, Bytes{0x80}});
    expect_encoding("04 08 FF", BitString{8, Bytes{0xff}});

    SECTION("有效位数与字节长度不匹配时拒绝") {
        // 9 位需要两字节，只给一字节时先在读取阶段报输入不足。
        const Bytes short_payload = hex("04 09 80");
        CHECK(codec::decode_data(short_payload).error().code == ErrorCode::need_more_data);
        // 给足两字节但声明 9 位而内容按8 位对齐时，长度校验通过，交由填充位校验。
        const Bytes exact = hex("04 08 FF");
        CHECK(codec::decode_data(exact));
    }

    SECTION("末字节未使用的低位必须为零") {
        const Bytes padded = hex("04 01 81");
        const Bytes zero_bits_with_byte = hex("04 00 80");
        CHECK(codec::decode_data(padded).error().code == ErrorCode::invalid_value);
        // 声明 0 位时不携带任何字节，多余的0x80 作为尾随数据被拒绝。
        CHECK(codec::decode_data(zero_bits_with_byte).error().code == ErrorCode::trailing_data);
    }

    SECTION("编码时校验填充位而非静默裁剪") {
        // 声明 1 位却给出 0xFF 表示末字节低位非零，属于非法值，编码必须拒绝。
        CHECK(codec::encode_data(BitString{1, Bytes{0xff}}).error().code ==
              ErrorCode::invalid_value);
        // 填充位合法时原样输出。
        auto encoded = codec::encode_data(BitString{1, Bytes{0x80}});
        REQUIRE(encoded);
        CHECK(encoded.value() == hex("04 01 80"));
    }
}

TEST_CASE("Data 长度字段使用多字节长格式", "[codec][data]") {
    SECTION("超过 127 字节的八位串使用长格式长度") {
        const OctetString long_string{Bytes(128, 0x41)};
        auto encoded = codec::encode_data(long_string);
        REQUIRE(encoded);
        CHECK(encoded.value()[0] == 0x09);
        CHECK(encoded.value()[1] == 0x81);
        CHECK(encoded.value()[2] == 0x80);
        auto decoded = codec::decode_data(encoded.value());
        REQUIRE(decoded);
        CHECK(decoded.value() == Data(long_string));
    }

    SECTION("127 字节仍可用短格式，超过则切换") {
        CHECK(codec::encode_data(OctetString{Bytes(127, 0x41)}).value()[1] == 127);
        CHECK(codec::encode_data(OctetString{Bytes(128, 0x41)}).value()[1] == 0x81);
    }

    SECTION("声明的长度超过剩余输入报 invalid_length") {
        const Bytes short_form = hex("09 80");
        const Bytes one_byte_form = hex("09 81 01 00");
        const Bytes two_byte_form = hex("09 82 00 80");
        CHECK(codec::decode_data(short_form).error().code == ErrorCode::invalid_length);
        CHECK(codec::decode_data(one_byte_form).error().code == ErrorCode::invalid_length);
        CHECK(codec::decode_data(two_byte_form).error().code == ErrorCode::invalid_length);
    }
}

TEST_CASE("Data 非法输入给出精确错误分类", "[codec][data]") {
    // 统一入口：内部先解析十六进制再建立视图，避免 ByteView 绑定临时容器。
    const auto decode = [](ByteView input, const Limits& limits) {
        return codec::decode_data(input, limits);
    };

    SECTION("语义非法的取值报 invalid_value") {
        require_decode_error("03 02", ErrorCode::invalid_value, decode);     // 布尔只允许 00/01。
        require_decode_error("04 01 81", ErrorCode::invalid_value, decode);  // 位串填充位非零。
        require_decode_error("0A 01 00", ErrorCode::invalid_value,
                             decode);  // 可见字符串含控制字符。
        require_decode_error("0C 02 C0 80", ErrorCode::invalid_value,
                             decode);  // UTF-8 非法首字节。
        require_decode_error("0C 03 ED A0 80", ErrorCode::invalid_value, decode);  // UTF-8 代理区。
        require_decode_error("55 01 00", ErrorCode::invalid_value, decode);     // TSA 长度为奇数。
        require_decode_error("54 06 00 01", ErrorCode::invalid_value, decode);  // TI 单位编码越界。
    }

    SECTION("未分配的标签报 unsupported_tag") {
        require_decode_error("07", ErrorCode::unsupported_tag, decode);
        require_decode_error("1D", ErrorCode::unsupported_tag, decode);
        require_decode_error("FF", ErrorCode::unsupported_tag, decode);
    }

    SECTION("完整输入后的多余字节报 trailing_data") {
        require_decode_error("00 00", ErrorCode::trailing_data, decode);
        require_decode_error("11 01 11 02", ErrorCode::trailing_data, decode);
    }

    SECTION("半个字段一律报 need_more_data 而非损坏") {
        require_decode_error("0F", ErrorCode::need_more_data, decode);
        require_decode_error("05 80 00", ErrorCode::need_more_data, decode);
        require_decode_error("09 03 00", ErrorCode::need_more_data, decode);
        require_decode_error("1B 17 3B", ErrorCode::need_more_data, decode);
        require_decode_error("50 40", ErrorCode::need_more_data, decode);
    }
}

TEST_CASE("Data 编解码遵守资源限制", "[codec][data]") {
    const auto decode = [](ByteView input, const Limits& limits) {
        return codec::decode_data(input, limits);
    };

    SECTION("节点总数上限") {
        Limits limits;
        limits.max_elements = 2;
        // 根节点加两个元素共三个节点，超出上限。
        require_decode_error("01 02 00 00", ErrorCode::resource_limit, decode, limits);
        CHECK(codec::encode_data(Array{{Null{}, Null{}}}, limits).error().code ==
              ErrorCode::resource_limit);
    }

    SECTION("上限恰好等于节点数时放行") {
        Limits limits;
        limits.max_elements = 3;
        const Bytes three_nodes = hex("01 02 00 00");
        CHECK(codec::decode_data(three_nodes, limits));
        CHECK(codec::encode_data(Array{{Null{}, Null{}}}, limits));
    }

    SECTION("嵌套深度上限，根节点深度为零") {
        // 深度按"容器层数"计：根容器深度为零，N 个连续 array 需要 max_depth >= N。
        // 因此 "01 01 00" 需要 1，"01 01 01 01 00" 需要 3。
        const Bytes two_levels = hex("01 01 00");
        const Bytes four_levels = hex("01 01 01 01 00");
        Limits limits;
        limits.max_depth = 0;
        require_decode_error("01 01 00", ErrorCode::resource_limit, decode, limits);
        limits.max_depth = 1;
        CHECK(codec::decode_data(two_levels, limits));
        require_decode_error("01 01 01 01 00", ErrorCode::resource_limit, decode, limits);
        limits.max_depth = 3;
        CHECK(codec::decode_data(four_levels, limits));
    }

    SECTION("输出字节上限") {
        Limits limits;
        limits.max_data_bytes = 2;
        CHECK(codec::encode_data(OctetString{Bytes(8, 0x41)}, limits).error().code ==
              ErrorCode::resource_limit);
    }
}

TEST_CASE("浮点位模式在往返后保持不变", "[codec][data]") {
    // NaN 用浮点语义比较恒为 false，只能验证原始位模式。
    const Bytes nan_bits = hex("17 7F C0 00 42");
    auto decoded = codec::decode_data(nan_bits);
    REQUIRE(decoded);
    CHECK(codec::encode_data(decoded.value()).value() == nan_bits);

    SECTION("正负零与无穷同样按位保留") {
        for (const char* bits : {"17 80 00 00 00", "17 FF 80 00 00", "18 00 00 00 00 00 00 00 00",
                                 "18 FF F0 00 00 00 00 00 00", "18 7F F0 00 00 00 00 00 00"}) {
            INFO("位模式=" << bits);
            const Bytes encoded = hex(bits);
            auto value = codec::decode_data(encoded);
            REQUIRE(value);
            CHECK(codec::encode_data(value.value()).value() == encoded);
        }
    }
}

TEST_CASE("流式读写接口与完整接口一致", "[codec][data]") {
    // array 的元素本身是 Data，因此嵌套结构需要显式再包一层 Data。
    const Data value = Array{{Data{Structure{{Data{UInt16{1}}, Data{UInt16{2}}}}},
                              Data{Structure{{Data{Boolean{true}}}}}}};

    SECTION("write_data 与 encode_data 产生相同字节") {
        Writer writer{64};
        codec::write_data(writer, value, Limits{});
        CHECK(writer.take() == codec::encode_data(value).value());
    }

    SECTION("read_data 与 decode_data 解析相同字节") {
        const Bytes bytes = codec::encode_data(value).value();
        Reader reader{ByteView{bytes}};
        CHECK(codec::read_data(reader, Limits{}) == value);
        CHECK_NOTHROW(reader.finish());
    }

    SECTION("显式起始深度参与上限计算") {
        // 递归实现以 depth 累加，调用方给出的起始深度须计入嵌套层数。
        const Bytes nested = hex("01 01 00");
        Reader reader{ByteView{nested}};
        Limits limits;
        limits.max_depth = 1;
        CHECK_DECODE_ERROR(codec::read_data(reader, limits, 1), ErrorCode::resource_limit);
    }

    SECTION("read_oad 与 write_oad 互逆") {
        const Oad oad{0x4001, 2, 5};
        Writer writer{8};
        codec::write_oad(writer, oad);
        const Bytes bytes = writer.take();
        CHECK(bytes == hex("40 01 02 05"));
        Reader reader{ByteView{bytes}};
        CHECK(codec::read_oad(reader) == oad);
    }

    SECTION("write_length 与 read_length 处理长格式") {
        for (std::size_t n : {0u, 1u, 127u, 128u, 255u, 256u, 65535u}) {
            Writer writer{8};
            codec::write_length(writer, n);
            const Bytes bytes = writer.take();
            Reader reader{ByteView{bytes}};
            CHECK(codec::read_length(reader, 1u << 20) == n);
        }
    }

    SECTION("write_length 遵守输出上限") {
        Writer writer{1};
        CHECK_DECODE_ERROR(codec::write_length(writer, 200), ErrorCode::resource_limit,
                           "output limit");
    }
}