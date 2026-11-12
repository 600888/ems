/**
 * @file mutation_test.cpp
 * @brief SET 与 ACTION 普通、列表请求响应编解码的单元测试。
 */
#include <dlt698/protocol/apdu/mutation.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol::apdu;
using dlt698::test::fixture;
using dlt698::test::hex;

namespace {

/**
 * @brief 校验 APDU 的编码字节与 tests/vectors 下的独立预期值一致。
 * @param[in] expected 规范向量文件名，不含目录。
 * @param[in] message 待校验的 SET/ACTION 消息。
 * @note 向量不由被测编码器生成，因此这是真正的独立比对而非自洽往返。
 */
void expect_vector(const char* expected, const MutationApdu& message) {
    INFO("向量=" << expected);
    auto encoded = encode_mutation(message);
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();
    const Bytes expected_bytes = fixture(expected);
    CHECK(to_hex(ByteView{bytes}) == to_hex(ByteView{expected_bytes}));

    auto decoded = decode_mutation(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value().index() == message.index());

    auto re_encoded = encode_mutation(decoded.value());
    REQUIRE(static_cast<bool>(re_encoded));
    const Bytes result = re_encoded.value();
    CHECK(to_hex(ByteView{result}) == to_hex(ByteView{expected_bytes}));
}

/**
 * @brief 断言 SET/ACTION APDU 解码失败，并同时比对错误分类与上下文字段。
 * @param[in] text 十六进制输入文本。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 */
void require_mutation_error(const char* text, ErrorCode code, const char* context) {
    INFO("输入=" << text);
    const Bytes bytes = hex(text);
    auto result = decode_mutation(ByteView{bytes});
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

/// 附录 D.4.1 的日期时间秒值：2016-01-20 16:27:11。
Data clock_value() { return Data{DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x10, 0x1b, 0x0b}}}; }

}  // namespace

TEST_CASE("SET 普通请求按附录 D.4.1 向量往返", "[apdu][mutation][set]") {
    SetRequest request;
    request.piid = 2;
    request.list = false;
    request.attributes = {SetAttribute{Oad{0x4000, 0x02, 0x00}, clock_value()}};

    expect_vector("set-normal-request.hex", MutationApdu{request});

    SECTION("请求字段与属性值逐项保持") {
        const Bytes bytes = fixture("set-normal-request.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetRequest>(decoded.value());
        CHECK(back.piid == 2);
        CHECK_FALSE(back.list);
        REQUIRE(back.attributes.size() == 1);
        CHECK(back.attributes[0].attribute.oi == 0x4000);
        CHECK(back.attributes[0].attribute.attribute == 0x02);
        // 属性值保留精确 Data 类型。
        REQUIRE(back.attributes[0].value.type() == DataType::date_time_s);
        const auto& value = std::get<DateTimeS>(back.attributes[0].value.payload).value;
        CHECK(value[0] == 0x07);
        CHECK(value[1] == 0xe0);
        CHECK(value[3] == 0x14);
        CHECK(value[4] == 0x10);
        CHECK(value[5] == 0x1b);
        CHECK(value[6] == 0x0b);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("PIID 保留位必须为零") {
        request.piid = 0x40;
        CHECK(encode_mutation(MutationApdu{request}).error().code == ErrorCode::invalid_value);
        require_mutation_error("06 01 40 40 00 02 00 1C 07 E0 01 14 10 1B 0B 00",
                               ErrorCode::invalid_value, "PIID reserved bit");
    }

    SECTION("普通形式恰含一项") {
        request.attributes = {SetAttribute{Oad{0x4000, 0x02, 0x00}, clock_value()},
                              SetAttribute{Oad{0x4001, 0x02, 0x00}, clock_value()}};
        auto encoded = encode_mutation(MutationApdu{request});
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::invalid_length);
        CHECK(encoded.error().context == "mutation count");

        request.attributes.clear();
        CHECK(encode_mutation(MutationApdu{request}).error().code == ErrorCode::invalid_length);
    }

    SECTION("任意属性值类型可往返") {
        const Data values[] = {Null{},
                               Data{Boolean{true}},
                               Data{Int32{-1}},
                               Data{UInt64{0xffffffffffffffffull}},
                               Data{OctetString{hex("DE AD BE EF")}},
                               Data{VisibleString{"abc"}},
                               Data{DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x10, 0x1b, 0x0b}}}};
        for (std::size_t i = 0; i < std::size(values); ++i) {
            INFO("值下标=" << i);
            SetRequest one;
            one.piid = 1;
            one.attributes = {SetAttribute{Oad{0x4000, 0x02, 0x00}, values[i]}};
            auto encoded = encode_mutation(MutationApdu{one});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_mutation(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            const auto& back = std::get<SetRequest>(decoded.value()).attributes[0].value;
            CHECK(back.type() == values[i].type());
        }
    }

    SECTION("时间标签可附加在请求尾部") {
        const TimeTag tag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        request.time_tag = tag;
        auto encoded = encode_mutation(MutationApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        const Bytes base = fixture("set-normal-request.hex");
        CHECK(bytes.size() == base.size() + 10);
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetRequest>(decoded.value()).time_tag;
        REQUIRE(back.has_value());
        CHECK(*back == tag);
    }
}

TEST_CASE("SET 普通响应按附录 D.4.1 向量往返", "[apdu][mutation][set]") {
    SetResponse response;
    response.piid_acd = 2;
    response.list = false;
    response.attributes = {SetResult{Oad{0x4000, 0x02, 0x00}, 0}};

    expect_vector("set-normal-response.hex", MutationApdu{response});

    SECTION("响应字段与 DAR 逐项保持") {
        const Bytes bytes = fixture("set-normal-response.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetResponse>(decoded.value());
        CHECK(back.piid_acd == 2);
        CHECK_FALSE(back.list);
        REQUIRE(back.attributes.size() == 1);
        CHECK(back.attributes[0].attribute.oi == 0x4000);
        // DAR 为零表示写入成功，外层解码同样成功。
        CHECK(back.attributes[0].dar == 0);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("外层成功不等于业务成功") {
        // DAR 非零表示该属性写入失败，但整条 APDU 解码仍然成功。
        for (int dar : {1, 2, 100, 255}) {
            INFO("DAR=" << dar);
            SetResponse partial;
            partial.piid_acd = 2;
            partial.list = true;
            partial.attributes = {
                SetResult{Oad{0x4000, 0x02, 0x00}, 0},
                SetResult{Oad{0x4001, 0x02, 0x00}, static_cast<std::uint8_t>(dar)}};
            auto encoded = encode_mutation(MutationApdu{partial});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_mutation(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            const auto& back = std::get<SetResponse>(decoded.value());
            REQUIRE(back.attributes.size() == 2);
            CHECK(back.attributes[0].dar == 0);
            // 远端 DAR 原样保留，由调用方映射为具体原因。
            CHECK(back.attributes[1].dar == dar);
        }
    }

    SECTION("不完整 FollowReport 选择符被拒绝") {
        const Bytes bytes = fixture("set-normal-response.hex");
        Bytes with_follow = bytes;
        with_follow[with_follow.size() - 2] = 1;
        auto decoded = decode_mutation(ByteView{with_follow});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "FollowReport choice");
    }
}

TEST_CASE("SET 列表按附录 D.4.2 向量往返", "[apdu][mutation][set]") {
    SECTION("列表请求携带多个属性与值") {
        SetRequest request;
        request.piid = 3;
        request.list = true;
        request.attributes = {
            SetAttribute{Oad{0x4001, 0x02, 0x00}, Data{OctetString{hex("00 00 00 00 00 01")}}},
            SetAttribute{Oad{0x4000, 0x02, 0x00}, clock_value()}};
        expect_vector("set-list-request.hex", MutationApdu{request});

        const Bytes bytes = fixture("set-list-request.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetRequest>(decoded.value());
        CHECK(back.piid == 3);
        CHECK(back.list);
        REQUIRE(back.attributes.size() == 2);
        // 列表内顺序必须保持，不按 OAD 重排。
        CHECK(back.attributes[0].attribute.oi == 0x4001);
        CHECK(back.attributes[1].attribute.oi == 0x4000);
        const auto& address = std::get<OctetString>(back.attributes[0].value.payload).value;
        CHECK(to_hex(ByteView{address}) == "00 00 00 00 00 01");
        REQUIRE(back.attributes[1].value.type() == DataType::date_time_s);
    }

    SECTION("列表响应逐项给出 DAR") {
        SetResponse response;
        response.piid_acd = 3;
        response.list = true;
        response.attributes = {SetResult{Oad{0x4001, 0x02, 0x00}, 0},
                               SetResult{Oad{0x4000, 0x02, 0x00}, 0}};
        expect_vector("set-list-response.hex", MutationApdu{response});

        const Bytes bytes = fixture("set-list-response.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetResponse>(decoded.value());
        CHECK(back.list);
        REQUIRE(back.attributes.size() == 2);
        // 响应顺序与向量的请求顺序一致。
        CHECK(back.attributes[0].attribute.oi == 0x4001);
        CHECK(back.attributes[1].attribute.oi == 0x4000);
    }

    SECTION("列表形式也可只含一项") {
        SetRequest request;
        request.piid = 3;
        request.list = true;
        request.attributes = {SetAttribute{Oad{0x4000, 0x02, 0x00}, clock_value()}};
        auto encoded = encode_mutation(MutationApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<SetRequest>(decoded.value());
        CHECK(back.list);
        CHECK(back.attributes.size() == 1);
    }

    SECTION("列表项数上限在编解码两侧生效") {
        SetRequest request;
        request.piid = 1;
        request.list = true;
        request.attributes = {SetAttribute{Oad{0x4001, 0x02, 0x00}, Null{}},
                              SetAttribute{Oad{0x4002, 0x02, 0x00}, Null{}}};
        Limits limits;
        limits.max_elements = 1;
        auto encoded = encode_mutation(MutationApdu{request}, limits);
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::invalid_length);
        CHECK(encoded.error().context == "mutation count");
        limits.max_elements = 2;
        CHECK(static_cast<bool>(encode_mutation(MutationApdu{request}, limits)));
    }

    SECTION("数量域超限时按伪数量拒绝") {
        // 声明两项但只给一项的字节，最小需求检查先于逐项读取。
        require_mutation_error("06 02 02 40 01 02 00 00", ErrorCode::need_more_data,
                               "mutation items");
    }
}

TEST_CASE("ACTION 普通请求按附录 D.5.1 向量往返", "[apdu][mutation][action]") {
    ActionRequest request;
    request.piid = 5;
    request.list = false;
    request.methods = {ActionMethod{Omd{0x0010, 0x01, 0x00}, Data{Int8{0}}}};

    expect_vector("action-normal-request.hex", MutationApdu{request});

    SECTION("方法描述符与参数逐项保持") {
        const Bytes bytes = fixture("action-normal-request.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionRequest>(decoded.value());
        CHECK(back.piid == 5);
        CHECK_FALSE(back.list);
        REQUIRE(back.methods.size() == 1);
        CHECK(back.methods[0].method.oi == 0x0010);
        CHECK(back.methods[0].method.method == 0x01);
        CHECK(back.methods[0].method.mode == 0x00);
        REQUIRE(back.methods[0].parameter.type() == DataType::int8);
        CHECK(std::get<Int8>(back.methods[0].parameter.payload).value == 0);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("方法模式的任意取值可往返") {
        for (int mode = 0; mode <= 255; ++mode) {
            INFO("模式=" << mode);
            ActionRequest one;
            one.piid = 1;
            one.methods = {
                ActionMethod{Omd{0x0010, 0x01, static_cast<std::uint8_t>(mode)}, Null{}}};
            auto encoded = encode_mutation(MutationApdu{one});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_mutation(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(std::get<ActionRequest>(decoded.value()).methods[0].method.mode == mode);
        }
    }

    SECTION("PIID 保留位必须为零") {
        request.piid = 0x40;
        CHECK(encode_mutation(MutationApdu{request}).error().code == ErrorCode::invalid_value);
        require_mutation_error("07 01 40 00 10 01 00 0F 00 00", ErrorCode::invalid_value,
                               "PIID reserved bit");
    }

    SECTION("普通形式恰含一项") {
        request.methods = {ActionMethod{Omd{0x0010, 0x01, 0x00}, Null{}},
                           ActionMethod{Omd{0x0010, 0x02, 0x00}, Null{}}};
        CHECK(encode_mutation(MutationApdu{request}).error().code == ErrorCode::invalid_length);
        request.methods.clear();
        CHECK(encode_mutation(MutationApdu{request}).error().code == ErrorCode::invalid_length);
    }

    SECTION("列表形式携带多个方法") {
        ActionRequest list;
        list.piid = 1;
        list.list = true;
        list.methods = {ActionMethod{Omd{0x0010, 0x01, 0x00}, Data{UInt8{1}}},
                        ActionMethod{Omd{0x0010, 0x02, 0x00}, Data{UInt8{2}}}};
        auto encoded = encode_mutation(MutationApdu{list});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionRequest>(decoded.value());
        CHECK(back.list);
        REQUIRE(back.methods.size() == 2);
        CHECK(back.methods[0].method.method == 0x01);
        CHECK(back.methods[1].method.method == 0x02);
        // 列表形式带两字节数量域，普通形式不带。
        CHECK(bytes[3] == 0x02);
    }
}

TEST_CASE("ACTION 返回值的 OPTIONAL 有两种状态", "[apdu][mutation][action]") {
    SECTION("没有返回数据时存在标记为零") {
        ActionResponse response;
        response.piid_acd = 5;
        response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, std::nullopt}};
        expect_vector("action-normal-response.hex", MutationApdu{response});

        const Bytes bytes = fixture("action-normal-response.hex");
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionResponse>(decoded.value()).methods[0];
        CHECK(back.dar == 0);
        // 没有返回数据是独立状态，不能与"返回 null"混淆。
        CHECK_FALSE(back.data.has_value());
    }

    SECTION("返回null 数据时存在标记为一") {
        ActionResponse response;
        response.piid_acd = 5;
        response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, Data{Null{}}}};
        auto encoded = encode_mutation(MutationApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 比"没有返回"恰好多一个 null 标签字节；存在标记的位置本就已存在。
        const Bytes base = fixture("action-normal-response.hex");
        CHECK(bytes.size() == base.size() + 1);
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionResponse>(decoded.value()).methods[0];
        REQUIRE(back.data.has_value());
        CHECK(back.data->type() == DataType::null);
    }

    SECTION("返回任意非空数据") {
        ActionResponse response;
        response.piid_acd = 5;
        response.methods = {
            ActionResult{Omd{0x0010, 0x01, 0x00}, 0, Data{Array{{Data{UInt16{7}}}}}}};
        auto encoded = encode_mutation(MutationApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionResponse>(decoded.value()).methods[0];
        REQUIRE(back.data.has_value());
        REQUIRE(back.data->type() == DataType::array);
    }

    SECTION("两种状态在解码后可区分") {
        ActionResponse absent;
        absent.piid_acd = 1;
        absent.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, std::nullopt}};
        ActionResponse present = absent;
        present.methods[0].data = Data{Null{}};

        auto absent_bytes = encode_mutation(MutationApdu{absent});
        REQUIRE(static_cast<bool>(absent_bytes));
        const Bytes without = absent_bytes.value();
        auto present_bytes = encode_mutation(MutationApdu{present});
        REQUIRE(static_cast<bool>(present_bytes));
        const Bytes with = present_bytes.value();
        CHECK(to_hex(ByteView{without}) != to_hex(ByteView{with}));

        auto decoded_without = decode_mutation(ByteView{without});
        auto decoded_with = decode_mutation(ByteView{with});
        REQUIRE(static_cast<bool>(decoded_without));
        REQUIRE(static_cast<bool>(decoded_with));
        CHECK_FALSE(std::get<ActionResponse>(decoded_without.value()).methods[0].data.has_value());
        CHECK(std::get<ActionResponse>(decoded_with.value()).methods[0].data.has_value());
    }

    SECTION("返回数据存在标记只能是 0 或 1") {
        ActionResponse response;
        response.piid_acd = 5;
        response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, Data{Null{}}}};
        auto encoded = encode_mutation(MutationApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        Bytes bytes = encoded.value();
        bytes[8] = 2;  // 返回数据存在标记
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "action Data presence");
    }

    SECTION("列表响应可逐项给出不同状态") {
        ActionResponse response;
        response.piid_acd = 5;
        response.list = true;
        response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, std::nullopt},
                            ActionResult{Omd{0x0010, 0x02, 0x00}, 1, Data{Null{}}}};
        auto encoded = encode_mutation(MutationApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ActionResponse>(decoded.value());
        CHECK(back.list);
        REQUIRE(back.methods.size() == 2);
        CHECK_FALSE(back.methods[0].data.has_value());
        CHECK(back.methods[1].dar == 1);
        CHECK(back.methods[1].data.has_value());
    }
}

TEST_CASE("SET 与 ACTION 的服务标签与变体分派", "[apdu][mutation]") {
    SECTION("四个服务标签互不混淆") {
        SetRequest set;
        set.piid = 1;
        set.attributes = {SetAttribute{Oad{0x4001, 0x02, 0x00}, Null{}}};
        SetResponse set_response;
        set_response.piid_acd = 1;
        set_response.attributes = {SetResult{Oad{0x4001, 0x02, 0x00}, 0}};
        ActionRequest action;
        action.piid = 1;
        action.methods = {ActionMethod{Omd{0x0010, 0x01, 0x00}, Null{}}};
        ActionResponse action_response;
        action_response.piid_acd = 1;
        action_response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, std::nullopt}};

        struct Case {
            std::uint8_t service;
            MutationApdu message;
        };

        const Case cases[] = {{0x06, MutationApdu{set}},
                              {0x86, MutationApdu{set_response}},
                              {0x07, MutationApdu{action}},
                              {0x87, MutationApdu{action_response}}};
        for (const auto& c : cases) {
            INFO("服务标签=" << std::hex << +c.service << std::dec);
            auto encoded = encode_mutation(c.message);
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            CHECK(bytes[0] == c.service);
            auto decoded = decode_mutation(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            // 解码必须回到同一个变体，而不是仅凭字节相近就通过。
            CHECK(decoded.value().index() == c.message.index());
        }
    }

    SECTION("非 SET 或 ACTION 服务报不支持") {
        for (const char* text : {"05 01 01 40 01 02 00 00", "85 01 01 40 01 02 00 01 00 00 00",
                                 "03 01 00", "08 01 01 00", "FF 01 01 00"}) {
            INFO("输入=" << text);
            const Bytes bytes = hex(text);
            auto result = decode_mutation(ByteView{bytes});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
            CHECK(result.error().context == "mutation service");
        }
    }

    SECTION("then-get 跟随上报暂不支持") {
        // 变体 3 表示 then-get，本实现不支持，报不支持而不是误当普通形式。
        require_mutation_error("06 03 01 40 01 02 00 00 00", ErrorCode::unsupported_service,
                               "then-get/variant");
        require_mutation_error("07 03 01 00 10 01 00 0F 00", ErrorCode::unsupported_service,
                               "then-get/variant");
        require_mutation_error("86 03 01 40 01 02 00 00 00 00", ErrorCode::unsupported_service,
                               "then-get/variant");
        require_mutation_error("87 03 01 00 10 01 00 00 00 00", ErrorCode::unsupported_service,
                               "then-get/variant");
    }

    SECTION("变体 0 与 3 及以上未分配") {
        for (int choice : {0, 3, 4, 0x0f}) {
            INFO("变体=" << choice);
            Bytes bytes{0x06, static_cast<std::uint8_t>(choice), 0x01};
            bytes.insert(bytes.end(), {0x40, 0x01, 0x02, 0x00, 0x00, 0x00});
            auto result = decode_mutation(ByteView{bytes}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
            CHECK(result.error().context == "then-get/variant");
        }
    }

    SECTION("响应侧 PIID-ACD 不校验保留位") {
        SetResponse response;
        response.piid_acd = 0x41;
        response.attributes = {SetResult{Oad{0x4001, 0x02, 0x00}, 0}};
        auto encoded = encode_mutation(MutationApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_mutation(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK((std::get<SetResponse>(decoded.value()).piid_acd & 0x40) != 0);
    }
}

TEST_CASE("SET 与 ACTION 的错误分类与资源上限", "[apdu][mutation]") {
    SECTION("尾随字节被拒绝") {
        const Bytes bytes = fixture("set-normal-request.hex");
        Bytes padded = bytes;
        padded.push_back(0x00);
        auto decoded = decode_mutation(ByteView{padded});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::trailing_data);
    }

    SECTION("逐字节截断全部拒绝") {
        const char* names[] = {"set-normal-request.hex",    "set-normal-response.hex",
                               "set-list-request.hex",      "set-list-response.hex",
                               "action-normal-request.hex", "action-normal-response.hex"};
        for (const char* name : names) {
            INFO("向量=" << name);
            const Bytes bytes = fixture(name);
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                INFO("截断到 " << n << " 字节");
                CHECK_FALSE(
                    static_cast<bool>(decode_mutation(ByteView{bytes}.subview(0, n), Limits{})));
            }
            CHECK(static_cast<bool>(decode_mutation(ByteView{bytes}, Limits{})));
        }
    }

    SECTION("APDU 字节上限在输入与输出两侧生效") {
        const Bytes bytes = fixture("set-list-request.hex");
        Limits limits;
        limits.max_data_bytes = 16;
        auto decoded = decode_mutation(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::resource_limit);
        CHECK(decoded.error().context == "APDU limit");

        SetRequest request;
        request.piid = 1;
        request.attributes = {SetAttribute{Oad{0x4001, 0x02, 0x00}, Null{}}};
        // 该请求恰好九字节，因此16 的上限仍然放行，须用更小的上限验证输出限制。
        CHECK(static_cast<bool>(encode_mutation(MutationApdu{request}, limits)));
        Limits tight;
        tight.max_data_bytes = 8;
        auto encoded = encode_mutation(MutationApdu{request}, tight);
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::resource_limit);
        CHECK(encoded.error().context == "output limit");
    }

    SECTION("单个 Data 树的深度预算逐层生效") {
        const Data inner{Array{{Data{UInt8{1}}}}};
        const Data middle{Array{{inner}}};
        const Data outer{Array{{middle}}};
        SetRequest request;
        request.piid = 1;
        request.attributes = {SetAttribute{Oad{0x4001, 0x02, 0x00}, outer}};
        auto encoded = encode_mutation(MutationApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();

        Limits limits;
        limits.max_depth = 2;
        auto too_deep = decode_mutation(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(too_deep));
        CHECK(too_deep.error().code == ErrorCode::resource_limit);
        limits.max_depth = 32;
        CHECK(static_cast<bool>(decode_mutation(ByteView{bytes}, limits)));
    }

    SECTION("值标签非法时报精确错误") {
        // 属性描述符之后是 Data 标签，未分配标签必须被识别而不是当作损坏数据。
        require_mutation_error("06 01 01 40 01 02 00 07 00", ErrorCode::unsupported_tag,
                               "Data tag");
        require_mutation_error("06 01 01 40 01 02 00 1D 00", ErrorCode::unsupported_tag,
                               "Data tag");
    }

    SECTION("值内部截断时报输入不足") {
        // 时间标签 1B 需要三个字节，只给两个时不得被静默补齐。
        require_mutation_error("06 01 01 40 01 02 00 1B 17 3B", ErrorCode::need_more_data,
                               "calendar field");
    }

    SECTION("固定字段截断时报输入不足") {
        // 服务、变体、PIID 之后缺方法描述符。
        require_mutation_error("07 01 01 00", ErrorCode::need_more_data, "mutation items");
        require_mutation_error("06 01 01 00", ErrorCode::need_more_data, "mutation items");
    }
}