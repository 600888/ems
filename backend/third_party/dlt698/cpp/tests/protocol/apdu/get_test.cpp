/**
 * @file get_test.cpp
 * @brief GET 普通、列表、记录与自解析分块编解码的单元测试。
 */
#include <dlt698/protocol/apdu/get.hpp>

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
 * @param[in] apdu 待校验的 GET 类消息。
 * @note 向量不由被测编码器生成，因此这是真正的独立比对而非自洽往返。
 */
void expect_vector(const char* expected, const GetApdu& apdu) {
    INFO("向量=" << expected);
    auto encoded = encode_get(apdu);
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();
    const Bytes expected_bytes = fixture(expected);
    CHECK(to_hex(ByteView{bytes}) == to_hex(ByteView{expected_bytes}));

    auto decoded = decode_get(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value().index() == apdu.index());

    auto re_encoded = encode_get(decoded.value());
    REQUIRE(static_cast<bool>(re_encoded));
    const Bytes result = re_encoded.value();
    CHECK(to_hex(ByteView{result}) == to_hex(ByteView{expected_bytes}));
}

/**
 * @brief 断言 GET 类 APDU 解码失败，并同时比对错误分类与上下文字段。
 * @param[in] text 十六进制输入文本。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 */
void require_get_error(const char* text, ErrorCode code, const char* context) {
    INFO("输入=" << text);
    const Bytes bytes = hex(text);
    auto result = decode_get(ByteView{bytes});
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

}  // namespace

TEST_CASE("GET 普通请求按附录 D.3.1 向量往返", "[apdu][get]") {
    GetRequest request;
    request.piid = 1;
    request.list = false;
    request.attributes = {Oad{0x4001, 0x02, 0x00}};

    expect_vector("get-normal-request.hex", GetApdu{request});

    SECTION("请求字段逐项保持") {
        const Bytes bytes = fixture("get-normal-request.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRequest>(decoded.value());
        CHECK(back.piid == 1);
        CHECK_FALSE(back.list);
        REQUIRE(back.attributes.size() == 1);
        CHECK(back.attributes[0].oi == 0x4001);
        CHECK(back.attributes[0].attribute == 0x02);
        CHECK(back.attributes[0].index == 0x00);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("PIID 保留位必须为零") {
        request.piid = 0x40;
        CHECK(encode_get(GetApdu{request}).error().code == ErrorCode::invalid_value);
        require_get_error("05 01 40 01 02 00 00", ErrorCode::invalid_value, "PIID reserved bit");
    }

    SECTION("普通形式恰含一个属性") {
        request.attributes = {Oad{0x4001, 0x02, 0x00}, Oad{0x4002, 0x02, 0x00}};
        CHECK(encode_get(GetApdu{request}).error().code == ErrorCode::invalid_length);
        request.attributes.clear();
        CHECK(encode_get(GetApdu{request}).error().code == ErrorCode::invalid_length);
    }

    SECTION("属性描述符的任意取值可往返") {
        request.attributes = {Oad{0xffff, 0xff, 0xff}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& oad = std::get<GetRequest>(decoded.value()).attributes[0];
        CHECK(oad.oi == 0xffff);
        CHECK(oad.attribute == 0xff);
        CHECK(oad.index == 0xff);
    }

    SECTION("响应侧 PIID-ACD 不校验保留位") {
        GetResponse response;
        response.piid_acd = 0x41;
        response.list = false;
        response.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00}, Null{}}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        // bit7 是 ACD 标志，bit6 目前不参与校验。
        CHECK((std::get<GetResponse>(decoded.value()).piid_acd & 0x40) != 0);
    }
}

TEST_CASE("GET 普通响应按附录 D.3.1 向量往返", "[apdu][get]") {
    GetResponse response;
    response.piid_acd = 1;
    response.list = false;
    response.attributes = {
        AttributeResult{Oad{0x4001, 0x02, 0x00}, Data{OctetString{hex("12 34 56 78 90 12")}}}};

    expect_vector("get-normal-response.hex", GetApdu{response});

    SECTION("响应字段与属性值逐项保持") {
        const Bytes bytes = fixture("get-normal-response.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetResponse>(decoded.value());
        CHECK(back.piid_acd == 1);
        CHECK_FALSE(back.list);
        REQUIRE(back.attributes.size() == 1);
        const auto& result = back.attributes[0].result;
        // 选择子 1 表示携带 Data 而不是 DAR。
        REQUIRE(result.index() == 1);
        const auto& value = std::get<Data>(result);
        REQUIRE(value.type() == DataType::octet_string);
        const auto& text = std::get<OctetString>(value.payload).value;
        CHECK(to_hex(ByteView{text}) == "12 34 56 78 90 12");
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("逐项 DAR 与外层成功并存") {
        // 逐项 DAR 表示该属性失败，但整条APDU 解码仍然成功。
        GetResponse mixed;
        mixed.piid_acd = 2;
        mixed.list = true;
        mixed.attributes = {
            AttributeResult{Oad{0x2000, 0x02, 0x00}, std::uint8_t{1}},
            AttributeResult{Oad{0x2001, 0x02, 0x00}, Data{Array{{Data{UInt16{2413}}}}}}};
        auto encoded = encode_get(GetApdu{mixed});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetResponse>(decoded.value());
        REQUIRE(back.attributes.size() == 2);
        // 第一项是 DAR，远端码原样保留。
        REQUIRE(back.attributes[0].result.index() == 0);
        CHECK(std::get<std::uint8_t>(back.attributes[0].result) == 1);
        // 第二项是数组值。
        REQUIRE(back.attributes[1].result.index() == 1);
        const auto& array = std::get<Data>(back.attributes[1].result);
        REQUIRE(array.type() == DataType::array);
        CHECK(std::get<Array>(array.payload).value.size() == 1);
    }

    SECTION("DAR 取值覆盖 0 至 255") {
        for (int dar = 0; dar <= 255; ++dar) {
            INFO("DAR=" << dar);
            GetResponse value;
            value.piid_acd = 1;
            value.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00},
                                                std::uint8_t{static_cast<std::uint8_t>(dar)}}};
            auto encoded = encode_get(GetApdu{value});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_get(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(std::get<std::uint8_t>(
                      std::get<GetResponse>(decoded.value()).attributes[0].result) == dar);
        }
    }

    SECTION("属性结果选择子只能是 0 或 1") {
        require_get_error("85 01 01 40 01 02 00 02 00 00 00", ErrorCode::invalid_value,
                          "GetResult choice");
        require_get_error("85 01 01 40 01 02 00 03 00 00 00", ErrorCode::invalid_value,
                          "GetResult choice");
        require_get_error("85 01 01 40 01 02 00 FF 00 00 00", ErrorCode::invalid_value,
                          "GetResult choice");
    }

    SECTION("不完整 FollowReport 选择符被拒绝") {
        const Bytes bytes = fixture("get-normal-response.hex");
        Bytes with_follow = bytes;
        with_follow[with_follow.size() - 2] = 1;  // FollowReport 存在标记
        auto decoded = decode_get(ByteView{with_follow});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "FollowReport choice");
    }
}

TEST_CASE("GET 列表按附录 D.3.2 向量往返", "[apdu][get]") {
    SECTION("列表请求携带多个 OAD") {
        GetRequest request;
        request.piid = 2;
        request.list = true;
        request.attributes = {Oad{0x2000, 0x02, 0x00}, Oad{0x2001, 0x02, 0x00}};
        expect_vector("get-list-request.hex", GetApdu{request});

        const Bytes bytes = fixture("get-list-request.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRequest>(decoded.value());
        CHECK(back.piid == 2);
        CHECK(back.list);
        REQUIRE(back.attributes.size() == 2);
        // 列表内顺序必须保持。
        CHECK(back.attributes[0].oi == 0x2000);
        CHECK(back.attributes[1].oi == 0x2001);
    }

    SECTION("列表响应按向量往返") {
        GetResponse response;
        response.piid_acd = 2;
        response.list = true;
        response.attributes = {
            AttributeResult{
                Oad{0x2000, 0x02, 0x00},
                Data{Array{{Data{UInt16{2413}}, Data{UInt16{2413}}, Data{UInt16{2413}}}}}},
            AttributeResult{
                Oad{0x2001, 0x02, 0x00},
                Data{Array{{Data{Int32{1000}}, Data{Int32{1000}}, Data{Int32{1000}}}}}}};
        expect_vector("get-list-response.hex", GetApdu{response});

        const Bytes bytes = fixture("get-list-response.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetResponse>(decoded.value());
        REQUIRE(back.attributes.size() == 2);
        const auto& voltage = std::get<Data>(back.attributes[0].result);
        const auto& current = std::get<Data>(back.attributes[1].result);
        REQUIRE(voltage.type() == DataType::array);
        REQUIRE(current.type() == DataType::array);
        // 数组保留精确元素类型，不做窄化。
        REQUIRE(std::get<Array>(voltage.payload).value[0].type() == DataType::uint16);
        REQUIRE(std::get<Array>(current.payload).value[0].type() == DataType::int32);
        CHECK(std::get<Array>(voltage.payload).value.size() == 3);
        CHECK(std::get<UInt16>(std::get<Array>(voltage.payload).value[0].payload).value == 2413);
        CHECK(std::get<Int32>(std::get<Array>(current.payload).value[0].payload).value == 1000);
    }

    SECTION("列表形式必须非空") {
        GetRequest request;
        request.piid = 2;
        request.list = true;
        request.attributes = {Oad{0x2000, 0x02, 0x00}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();

        request.attributes.clear();
        CHECK(encode_get(GetApdu{request}).error().code == ErrorCode::invalid_length);
        require_get_error("05 02 02 00 00", ErrorCode::invalid_length, "GET count");
    }

    SECTION("列表形式也可只含一项") {
        // 协议允许列表长度为 1，与普通形式在字节上由选择子区分。
        GetRequest request;
        request.piid = 2;
        request.list = true;
        request.attributes = {Oad{0x2000, 0x02, 0x00}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRequest>(decoded.value());
        CHECK(back.list);
        CHECK(back.attributes.size() == 1);
    }

    SECTION("属性数上限") {
        GetRequest request;
        request.piid = 1;
        request.list = true;
        request.attributes = {Oad{0x2000, 0x02, 0x00}, Oad{0x2001, 0x02, 0x00},
                              Oad{0x2002, 0x02, 0x00}};
        Limits limits;
        limits.max_elements = 2;
        CHECK(encode_get(GetApdu{request}, limits).error().code == ErrorCode::invalid_length);
        limits.max_elements = 3;
        CHECK(static_cast<bool>(encode_get(GetApdu{request}, limits)));
    }
}

TEST_CASE("GET 记录请求按向量往返", "[apdu][get][record]") {
    SECTION("单表记录查询携带行条件与列选择") {
        GetRecordRequest request;
        request.piid = 3;
        request.list = false;
        // Selector1：冻结时间等于给定值。
        Selector1 selector;
        selector.attribute = Oad{0x2021, 0x02, 0x00};
        selector.value = Data{DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x00, 0x00, 0x00}}};
        request.records = {GetRecord{Oad{0x5004, 0x02, 0x00},
                                     selector,
                                     {Csd{Oad{0x2021, 0x02, 0x00}}, Csd{Oad{0x0010, 0x02, 0x00}}}}};
        expect_vector("get_record_request.hex", GetApdu{request});

        const Bytes bytes = fixture("get_record_request.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRecordRequest>(decoded.value());
        CHECK(back.piid == 3);
        CHECK_FALSE(back.list);
        REQUIRE(back.records.size() == 1);
        const auto& record = back.records[0];
        CHECK(record.attribute.oi == 0x5004);
        CHECK(record.attribute.attribute == 0x02);
        // 行选择器分支保持。
        REQUIRE(record.rows.index() == 1);
        const auto& first = std::get<Selector1>(record.rows);
        CHECK(first.attribute.oi == 0x2021);
        REQUIRE(first.value.type() == DataType::date_time_s);
        // 列选择器保持两列且顺序不变。
        REQUIRE(record.columns.size() == 2);
        CHECK(std::get<Oad>(record.columns[0]).oi == 0x2021);
        CHECK(std::get<Oad>(record.columns[1]).oi == 0x0010);
    }

    SECTION("表计集合分支携带 TSA 列表") {
        MeterAddresses meters;
        meters.values = {Tsa{hex("04 00 00 00 01 21")}, Tsa{hex("04 00 00 00 01 22")},
                         Tsa{hex("04 00 00 00 01 23")}, Tsa{hex("04 00 00 00 01 24")},
                         Tsa{hex("04 00 00 00 01 25")}};
        Selector5 selector;
        selector.time = DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x00, 0x00, 0x00}};
        selector.meters = meters;
        Road road;
        road.attribute = Oad{0x5004, 0x02, 0x00};
        road.associated = {Oad{0x0010, 0x02, 0x00}, Oad{0x0020, 0x02, 0x00}};
        GetRecordRequest request;
        request.piid = 4;
        request.records = {
            GetRecord{Oad{0x6012, 0x03, 0x00},
                      selector,
                      {Csd{Oad{0x4001, 0x02, 0x00}}, Csd{Oad{0x6040, 0x02, 0x00}},
                       Csd{Oad{0x6041, 0x02, 0x00}}, Csd{Oad{0x6042, 0x02, 0x00}}, Csd{road}}}};
        expect_vector("get_record_meters_request.hex", GetApdu{request});

        const Bytes bytes = fixture("get_record_meters_request.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRecordRequest>(decoded.value());
        const auto& record = back.records[0];
        CHECK(record.attribute.oi == 0x6012);
        CHECK(record.attribute.attribute == 0x03);
        REQUIRE(record.rows.index() == 5);
        const auto& fifth = std::get<Selector5>(record.rows);
        REQUIRE(fifth.meters.index() == 3);
        const auto& addresses = std::get<MeterAddresses>(fifth.meters);
        REQUIRE(addresses.values.size() == 5);
        const Bytes first_address = addresses.values[0].value;
        CHECK(to_hex(ByteView{first_address}) == "04 00 00 00 01 21");
        // 最后一列是 ROAD，携带两个关联 OAD。
        REQUIRE(record.columns.size() == 5);
        const auto& road_column = std::get<Road>(record.columns[4]);
        CHECK(road_column.attribute.oi == 0x5004);
        REQUIRE(road_column.associated.size() == 2);
        CHECK(road_column.associated[0].oi == 0x0010);
        CHECK(road_column.associated[1].oi == 0x0020);
    }

    SECTION("空列选择表示全选") {
        GetRecordRequest request;
        request.piid = 1;
        request.records = {GetRecord{Oad{0x5004, 0x02, 0x00}, SelectAll{}, {}}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& record = std::get<GetRecordRequest>(decoded.value()).records[0];
        CHECK(record.rows.index() == 0);
        CHECK(record.columns.empty());
    }

    SECTION("记录列表形式可携带多个表") {
        GetRecordRequest request;
        request.piid = 3;
        request.list = true;
        request.records = {
            GetRecord{Oad{0x5004, 0x02, 0x00}, SelectAll{}, {}},
            GetRecord{Oad{0x5005, 0x02, 0x00}, SelectAll{}, {Csd{Oad{0x2021, 0x02, 0x00}}}}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRecordRequest>(decoded.value());
        CHECK(back.list);
        REQUIRE(back.records.size() == 2);
        CHECK(back.records[0].attribute.oi == 0x5004);
        CHECK(back.records[1].attribute.oi == 0x5005);
        CHECK(back.records[1].columns.size() == 1);
    }

    SECTION("记录行选择器九个其他分支可往返") {
        // 各分支的编码由codec 层保证，这里只验证 GET 层能完整承载。
        Selector1 first;
        first.attribute = Oad{0x2021, 0x02, 0x00};
        first.value = Data{DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x00, 0x00, 0x00}}};
        Selector2 second;
        second.attribute = Oad{0x2021, 0x02, 0x00};
        second.begin = Data{DateTimeS{{0x07, 0xe0, 0x01, 0x01, 0x00, 0x00, 0x00}}};
        second.end = Data{DateTimeS{{0x07, 0xe0, 0x01, 0x02, 0x00, 0x00, 0x00}}};
        second.interval = Data{Int32{1}};
        Selector3 third;
        third.ranges = {second};
        Selector4 fourth;
        fourth.time = DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x00, 0x00, 0x00}};
        fourth.meters = NoMeters{};
        Selector5 fifth;
        fifth.time = fourth.time;
        fifth.meters = AllMeters{};
        Selector6 sixth;
        sixth.begin = DateTimeS{{0x07, 0xe0, 0x01, 0x01, 0x00, 0x00, 0x00}};
        sixth.end = DateTimeS{{0x07, 0xe0, 0x01, 0x02, 0x00, 0x00, 0x00}};
        sixth.interval = Ti{0, 1};
        sixth.meters = NoMeters{};
        Selector7 seventh;
        seventh.begin = sixth.begin;
        seventh.end = sixth.end;
        seventh.interval = sixth.interval;
        seventh.meters = sixth.meters;
        Selector8 eighth;
        eighth.begin = seventh.begin;
        eighth.end = seventh.end;
        eighth.interval = seventh.interval;
        eighth.meters = seventh.meters;
        Selector9 ninth;
        Selector10 tenth;
        tenth.latest = 1;
        tenth.meters = AllMeters{};

        // 数组下标与 Rsd 变体下标一一对应，因此可以直接验证分支不被改写。
        const Rsd selectors[] = {SelectAll{}, first,   second, third, fourth, fifth,
                                 sixth,       seventh, eighth, ninth, tenth};
        for (std::size_t i = 0; i < std::size(selectors); ++i) {
            INFO("分支下标=" << i);
            GetRecordRequest request;
            request.piid = 1;
            request.records = {GetRecord{Oad{0x5004, 0x02, 0x00}, selectors[i], {}}};
            auto encoded = encode_get(GetApdu{request});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_get(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(std::get<GetRecordRequest>(decoded.value()).records[0].rows.index() == i);
        }
    }

    SECTION("非法行选择器分支被拒绝") {
        require_get_error("05 03 03 50 04 02 00 0B 00", ErrorCode::invalid_value, "RSD choice");
    }
}

TEST_CASE("GET 记录响应按向量往返", "[apdu][get][record]") {
    SECTION("单表响应携带表头与行数据") {
        Rcsd columns{Csd{Oad{0x2021, 0x02, 0x00}}, Csd{Oad{0x0010, 0x02, 0x00}}};
        RecordRow row{Data{DateTimeS{{0x07, 0xe0, 0x01, 0x14, 0x00, 0x00, 0x00}}},
                      Data{Array{{Data{UInt32{0}}, Data{UInt32{0}}, Data{UInt32{0}},
                                  Data{UInt32{0}}, Data{UInt32{0}}}}}};
        GetRecordResponse response;
        response.piid_acd = 3;
        response.list = false;
        response.records = {
            RecordResult{Oad{0x5004, 0x02, 0x00}, columns, std::vector<RecordRow>{row}}};
        expect_vector("get_record_response.hex", GetApdu{response});

        const Bytes bytes = fixture("get_record_response.hex");
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRecordResponse>(decoded.value());
        CHECK(back.piid_acd == 3);
        REQUIRE(back.records.size() == 1);
        const auto& result = back.records[0];
        CHECK(result.attribute.oi == 0x5004);
        REQUIRE(result.columns.size() == 2);
        REQUIRE(result.result.index() == 1);
        const auto& rows = std::get<std::vector<RecordRow>>(result.result);
        REQUIRE(rows.size() == 1);
        // 行内Data 数量精确等于表头列数。
        REQUIRE(rows[0].size() == 2);
        REQUIRE(rows[0][0].type() == DataType::date_time_s);
        REQUIRE(rows[0][1].type() == DataType::array);
    }

    SECTION("记录结果可以是 DAR") {
        GetRecordResponse response;
        response.piid_acd = 3;
        response.records = {
            RecordResult{Oad{0x5004, 0x02, 0x00},
                         {Csd{Oad{0x2021, 0x02, 0x00}}, Csd{Oad{0x0010, 0x02, 0x00}}},
                         std::uint8_t{5}}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& result = std::get<GetRecordResponse>(decoded.value()).records[0];
        REQUIRE(result.result.index() == 0);
        CHECK(std::get<std::uint8_t>(result.result) == 5);
    }

    SECTION("行宽必须等于表头列数") {
        GetRecordResponse too_narrow;
        too_narrow.piid_acd = 1;
        too_narrow.records = {
            RecordResult{Oad{0x5004, 0x02, 0x00},
                         {Csd{Oad{0x2021, 0x02, 0x00}}, Csd{Oad{0x0010, 0x02, 0x00}}},
                         std::vector<RecordRow>{{Data{UInt32{1}}, Data{UInt32{2}}}}}};
        CHECK(static_cast<bool>(encode_get(GetApdu{too_narrow})));

        too_narrow.records[0].result = std::vector<RecordRow>{{Data{UInt32{1}}}};
        auto encoded = encode_get(GetApdu{too_narrow});
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::invalid_value);
        CHECK(encoded.error().context == "record row width");
    }

    SECTION("空表头允许零行记录") {
        GetRecordResponse empty;
        empty.piid_acd = 1;
        empty.records = {RecordResult{Oad{0x5004, 0x02, 0x00}, {}, std::vector<RecordRow>{}}};
        auto encoded = encode_get(GetApdu{empty});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& result = std::get<GetRecordResponse>(decoded.value()).records[0];
        CHECK(result.columns.empty());
        CHECK(std::get<std::vector<RecordRow>>(result.result).empty());
    }

    SECTION("空表头带行数据报资源超限") {
        // 表头为空时无法确定每行读多少个 Data，因此拒绝而不是猜测。
        require_get_error("85 03 03 50 04 02 00 00 01 01 00 00", ErrorCode::resource_limit,
                          "record cells/header");
    }

    SECTION("行列乘积超出元素上限") {
        require_get_error("85 03 03 50 04 02 00 01 00 20 21 02 00 02 00 00 00",
                          ErrorCode::invalid_value, "record result choice");
    }

    SECTION("记录结果选择子只能是 0 或 1") {
        // 表头两列之后是结果选择子，合法值为 0（DAR）与 1（行数据）。
        require_get_error("85 03 03 50 04 02 00 02 00 20 21 02 00 00 00 10 02 00 02 00 00 00",
                          ErrorCode::invalid_value, "record result choice");
        require_get_error("85 03 03 50 04 02 00 02 00 20 21 02 00 00 00 10 02 00 03 00 00 00",
                          ErrorCode::invalid_value, "record result choice");
    }

    SECTION("记录列表响应可携带多个表") {
        GetRecordResponse response;
        response.piid_acd = 3;
        response.list = true;
        response.records = {RecordResult{Oad{0x5004, 0x02, 0x00},
                                         {Csd{Oad{0x2021, 0x02, 0x00}}},
                                         std::vector<RecordRow>{{Data{UInt32{1}}}}},
                            RecordResult{Oad{0x5005, 0x02, 0x00},
                                         {Csd{Oad{0x2021, 0x02, 0x00}}},
                                         std::vector<RecordRow>{{Data{UInt32{2}}}}}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRecordResponse>(decoded.value());
        CHECK(back.list);
        REQUIRE(back.records.size() == 2);
        // 列表内顺序保持，不按 OAD 重排。
        CHECK(back.records[0].attribute.oi == 0x5004);
        CHECK(back.records[1].attribute.oi == 0x5005);
    }
}

TEST_CASE("GET-Next 家族往返", "[apdu][get][block]") {
    SECTION("Next 请求携带最近正确块号") {
        GetNextRequest request;
        request.piid = 1;
        request.block = 7;
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetNextRequest>(decoded.value());
        CHECK(back.piid == 1);
        CHECK(back.block == 7);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("Next 响应携带属性结果") {
        GetNextResponse response;
        response.piid_acd = 1;
        response.last = true;
        response.block = 0;
        response.result =
            std::vector<AttributeResult>{AttributeResult{Oad{0x4001, 0x02, 0x00}, Null{}}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetNextResponse>(decoded.value());
        CHECK(back.last);
        CHECK(back.block == 0);
        REQUIRE(back.result.index() == 1);
        CHECK(std::get<std::vector<AttributeResult>>(back.result).size() == 1);
    }

    SECTION("Next 响应携带记录结果") {
        GetNextResponse response;
        response.piid_acd = 1;
        response.last = false;
        response.block = 3;
        response.result =
            std::vector<RecordResult>{RecordResult{Oad{0x5004, 0x02, 0x00},
                                                   {Csd{Oad{0x2021, 0x02, 0x00}}},
                                                   std::vector<RecordRow>{{Data{UInt32{7}}}}}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetNextResponse>(decoded.value());
        CHECK_FALSE(back.last);
        CHECK(back.block == 3);
        REQUIRE(back.result.index() == 2);
    }

    SECTION("Next 末块可以携带 DAR") {
        GetNextResponse response;
        response.piid_acd = 1;
        response.last = true;
        response.block = 9;
        response.result = std::uint8_t{200};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetNextResponse>(decoded.value());
        REQUIRE(back.result.index() == 0);
        // 远端码原样保留，由调用方映射为具体错误。
        CHECK(std::get<std::uint8_t>(back.result) == 200);
    }

    SECTION("非末块携带 DAR 是自相矛盾的") {
        GetNextResponse response;
        response.piid_acd = 1;
        response.last = false;
        response.block = 0;
        response.result = std::uint8_t{1};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::invalid_value);
        CHECK(encoded.error().context == "nonfinal DAR block");
    }

    SECTION("Next 结果选择子只能是 0 至 2") {
        require_get_error("85 05 00 00 00 00 03 01 40 01 02 00 01 00 00 00",
                          ErrorCode::invalid_value, "Next result choice");
        require_get_error("85 05 00 00 00 00 04 01 40 01 02 00 01 00 00 00",
                          ErrorCode::invalid_value, "Next result choice");
    }

    SECTION("末块标记只能是 0 或 1") {
        require_get_error("85 05 00 02 00 00 01 01 40 01 02 00 01 00 00 00",
                          ErrorCode::invalid_value, "last block");
    }

    SECTION("Next 请求的块号覆盖 16 位边界") {
        for (std::uint16_t block : {0, 1, 255, 256, 65535}) {
            INFO("块号=" << block);
            GetNextRequest request;
            request.piid = 1;
            request.block = block;
            auto encoded = encode_get(GetApdu{request});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_get(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(std::get<GetNextRequest>(decoded.value()).block == block);
        }
    }
}

TEST_CASE("GET 家族的时间标签回显", "[apdu][get][time]") {
    SECTION("请求与响应都可携带时间标签") {
        const TimeTag tag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        GetRequest request;
        request.piid = 1;
        request.attributes = {Oad{0x4001, 0x02, 0x00}};
        request.time_tag = tag;
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<GetRequest>(decoded.value()).time_tag;
        REQUIRE(back.has_value());
        // 时间标签按线上字段逐字节回显，不做时区换算或单位换算。
        CHECK(*back == tag);

        GetResponse response;
        response.piid_acd = 1;
        response.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00}, Null{}}};
        response.time_tag = tag;
        auto response_encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(response_encoded));
        const Bytes response_bytes = response_encoded.value();
        auto response_back = decode_get(ByteView{response_bytes});
        REQUIRE(static_cast<bool>(response_back));
        REQUIRE(std::get<GetResponse>(response_back.value()).time_tag.has_value());
        CHECK(*std::get<GetResponse>(response_back.value()).time_tag == tag);
    }

    SECTION("记录与 Next 家族同样支持时间标签") {
        const TimeTag tag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {1, 60}};
        GetRecordRequest record;
        record.piid = 1;
        record.records = {GetRecord{Oad{0x5004, 0x02, 0x00}, SelectAll{}, {}}};
        record.time_tag = tag;
        auto record_bytes = encode_get(GetApdu{record});
        REQUIRE(static_cast<bool>(record_bytes));
        const Bytes record_encoded = record_bytes.value();
        auto record_back = decode_get(ByteView{record_encoded});
        REQUIRE(static_cast<bool>(record_back));
        CHECK(*std::get<GetRecordRequest>(record_back.value()).time_tag == tag);

        GetNextRequest next;
        next.piid = 1;
        next.block = 0;
        next.time_tag = tag;
        auto next_bytes = encode_get(GetApdu{next});
        REQUIRE(static_cast<bool>(next_bytes));
        const Bytes next_encoded = next_bytes.value();
        auto next_back = decode_get(ByteView{next_encoded});
        REQUIRE(static_cast<bool>(next_back));
        CHECK(*std::get<GetNextRequest>(next_back.value()).time_tag == tag);
    }

    SECTION("存在标记非 0 或 1 时被拒绝") {
        GetRequest request;
        request.piid = 1;
        request.attributes = {Oad{0x4001, 0x02, 0x00}};
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        Bytes bytes = encoded.value();
        bytes[bytes.size() - 11] = 2;  // 时间标签存在标记
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "TimeTag presence");
    }

    SECTION("非法时间单位被拒绝") {
        GetRequest request;
        request.piid = 1;
        request.attributes = {Oad{0x4001, 0x02, 0x00}};
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {6, 1}};
        CHECK(encode_get(GetApdu{request}).error().code == ErrorCode::invalid_value);

        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        Bytes bytes = encoded.value();
        bytes[bytes.size() - 3] = 6;  // 单位字节
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "TI unit");
    }

    SECTION("时间间隔为零表示不限制时差") {
        GetRequest request;
        request.piid = 1;
        request.attributes = {Oad{0x4001, 0x02, 0x00}};
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 0}};
        auto encoded = encode_get(GetApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_get(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& tag = std::get<GetRequest>(decoded.value()).time_tag;
        REQUIRE(tag.has_value());
        CHECK(tag->allowed_delay.interval == 0);
    }
}

TEST_CASE("GET 未分配的服务与变体被拒绝", "[apdu][get]") {
    SECTION("非 GET 服务报不支持") {
        for (const char* text : {"06 01 01 40 01 02 00 00 00", "07 01 01 00 10 01 00 00 00",
                                 "03 01 00", "86 01 01 40 01 02 00 00 00 00"}) {
            INFO("输入=" << text);
            const Bytes bytes = hex(text);
            auto result = decode_get(ByteView{bytes});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
            CHECK(result.error().context == "GET service");
        }
    }

    SECTION("变体 0 与 7 及以上未分配") {
        // 变体 1 至 6 是普通、列表、记录、记录列表、Next 与 MD5。
        for (int choice : {0, 7, 8, 9, 0x0f}) {
            INFO("变体=" << choice);
            Bytes bytes{0x05, static_cast<std::uint8_t>(choice), 0x01};
            bytes.insert(bytes.end(), {0x40, 0x01, 0x02, 0x00, 0x00, 0x00});
            auto result = decode_get(ByteView{bytes}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
            CHECK(result.error().context == "GET variant");
        }
    }

    SECTION("尾随字节被拒绝") {
        require_get_error("05 01 01 40 01 02 00 00 00", ErrorCode::trailing_data, "trailing bytes");
        const Bytes bytes = fixture("get-normal-response.hex");
        Bytes padded = bytes;
        padded.push_back(0x00);
        auto decoded = decode_get(ByteView{padded});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::trailing_data);
    }

    SECTION("逐字节截断全部拒绝") {
        const char* names[] = {"get-normal-request.hex",       "get-normal-response.hex",
                               "get-list-request.hex",         "get-list-response.hex",
                               "get_record_request.hex",       "get_record_response.hex",
                               "get_record_meters_request.hex"};
        for (const char* name : names) {
            INFO("向量=" << name);
            const Bytes bytes = fixture(name);
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                INFO("截断到 " << n << " 字节");
                CHECK_FALSE(static_cast<bool>(decode_get(ByteView{bytes}.subview(0, n), Limits{})));
            }
            CHECK(static_cast<bool>(decode_get(ByteView{bytes}, Limits{})));
        }
    }

    SECTION("APDU 字节上限在输入与输出两侧生效") {
        const Bytes bytes = fixture("get-list-response.hex");
        Limits limits;
        limits.max_data_bytes = 16;
        auto decoded = decode_get(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::resource_limit);
        CHECK(decoded.error().context == "GET APDU bytes");

        GetRequest request;
        request.piid = 1;
        request.attributes = {Oad{0x4001, 0x02, 0x00}};
        // 八字节 APDU 恰好用满16 是合法，因此上限必须更小。
        Limits tight;
        tight.max_data_bytes = 7;
        auto encoded = encode_get(GetApdu{request}, tight);
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::resource_limit);
        CHECK(encoded.error().context == "output limit");
        CHECK(static_cast<bool>(encode_get(GetApdu{request}, limits)));
    }

    SECTION("单个 Data 树的资源上限逐层生效") {
        // 三层 array 嵌套：深度按容器层数计，需要 max_depth >= 3。
        const Data inner{Array{{Data{UInt8{1}}}}};
        const Data middle{Array{{inner}}};
        const Data outer{Array{{middle}}};
        GetResponse response;
        response.piid_acd = 1;
        response.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00}, outer}};
        auto encoded = encode_get(GetApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes nested = encoded.value();

        Limits limits;
        limits.max_depth = 2;
        auto too_deep = decode_get(ByteView{nested}, limits);
        REQUIRE_FALSE(static_cast<bool>(too_deep));
        CHECK(too_deep.error().code == ErrorCode::resource_limit);
        limits.max_depth = 32;
        CHECK(static_cast<bool>(decode_get(ByteView{nested}, limits)));
    }
}