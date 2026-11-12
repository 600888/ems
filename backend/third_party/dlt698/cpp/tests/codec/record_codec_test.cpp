/**
 * @file record_codec_test.cpp
 * @brief 记录描述符 RSD、列选择 RCSD 与表计选择 MS 编解码的单元测试。
 * @note 期望字节来自 tests/vectors 下的独立规范向量，不由本实现生成。
 */
#include <dlt698/codec/record_codec.hpp>
#include <dlt698/dlt698.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol;
using dlt698::test::fixture;
using dlt698::test::hex;

namespace {

/**
 * @brief 用向量中的字节校验某个记录请求的完整 APDU 结构。
 * @param[in] name 向量文件名。
 */
void check_record_request(const char* name) {
    const Bytes bytes = fixture(name);
    auto decoded = apdu::decode_get(bytes);
    REQUIRE(static_cast<bool>(decoded));
    auto encoded = apdu::encode_get(decoded.value());
    REQUIRE(static_cast<bool>(encoded));
    CHECK(to_hex(ByteView{encoded.value()}) == to_hex(ByteView{bytes}));
    dlt698::test::require_truncation_rejected(
        [](ByteView input) { return apdu::decode_get(input); }, ByteView{bytes});
}

/**
 * @brief 校验 RSD 字节的往返一致性。
 * @param[in] bytes 期望字节，标签已包含。
 * @param[in] expected 期望的描述符模型。
 */
void expect_rsd(const char* bytes, const Rsd& expected) {
    const Bytes input = hex(bytes);
    Reader reader{ByteView{input}};
    const Rsd value = codec::read_rsd(reader);
    CHECK(value == expected);
    CHECK(reader.remaining() == 0);
    Writer writer{64};
    codec::write_rsd(writer, value, Limits{});
    const Bytes encoded = writer.take();
    CHECK(to_hex(ByteView{encoded}) == to_hex(ByteView{input}));
}

}  // namespace

TEST_CASE("记录请求向量按规范字节往返", "[codec][record]") {
    check_record_request("get_record_request.hex");
    check_record_request("get_record_meters_request.hex");
}

TEST_CASE("Selector1 冻结时间查询", "[codec][record]") {
    // get_record_request.hex 的 RSD 部分：属性 50040200 + 冻结时间 date_time_s。
    const Bytes bytes = fixture("get_record_request.hex");
    auto decoded = apdu::decode_get(bytes);
    REQUIRE(static_cast<bool>(decoded));
    CHECK(to_hex(ByteView{bytes}) ==
          "05 03 03 50 04 02 00 01 20 21 02 00 1C 07 E0 01 14 00 00 00 02 00 20 21 02 00 00 00 "
          "10 02 00 00");
    const auto* request = std::get_if<apdu::GetRecordRequest>(&decoded.value());
    REQUIRE(request != nullptr);
    CHECK(request->piid == 0x03);
    REQUIRE(request->records.size() == 1);
    const auto& query = request->records.front();
    CHECK(query.attribute == (Oad{0x5004, 2, 0}));
    // 冻结时间为 2016-01-20 00:00:00，秒与毫秒字段保留 FF 表示未指定。
    const auto* selector = std::get_if<Selector1>(&query.rows);
    REQUIRE(selector != nullptr);
    CHECK(selector->attribute == (Oad{0x2021, 2, 0}));
    const auto& frozen = std::get<DateTimeS>(selector->value.payload);
    CHECK(frozen.value[0] == 0x07);
    CHECK(frozen.value[1] == 0xe0);
    CHECK(frozen.value[2] == 1);
    CHECK(frozen.value[3] == 20);
    // RCSD 两列：日冻结总以及当前费率。
    REQUIRE(query.columns.size() == 2);
    CHECK(query.columns[0] == Csd{Oad{0x2021, 2, 0}});
    // 第二列为 0x00100200，即 OI=0x0010（当前费率）。
    CHECK(query.columns[1] == (Csd{Oad{0x0010, 2, 0}}));
}

TEST_CASE("RCSD 支持 OAD 与 ROAD 两种分支", "[codec][record]") {
    SECTION("ROAD 分支带关联属性列表") {
        const Bytes bytes = fixture("get_record_meters_request.hex");
        auto decoded = apdu::decode_get(bytes);
        REQUIRE(static_cast<bool>(decoded));
        const auto* request = std::get_if<apdu::GetRecordRequest>(&decoded.value());
        REQUIRE(request != nullptr);
        REQUIRE(request->records.size() == 1);
        const auto& columns = request->records.front().columns;
        REQUIRE(columns.size() == 5);
        // 前四列为普通 OAD。
        for (std::size_t i = 0; i < 4; ++i) CHECK(std::holds_alternative<Oad>(columns[i]));
        // 末列为 ROAD，含两个关联 OAD。
        const auto* road = std::get_if<Road>(&columns.back());
        REQUIRE(road != nullptr);
        CHECK(road->attribute == (Oad{0x5004, 2, 0}));
        REQUIRE(road->associated.size() == 2);
        // 关联 OAD 为 0x00100200 与 0x00200200，索引字节均为零。
        CHECK(road->associated[0] == (Oad{0x0010, 2, 0}));
        CHECK(road->associated[1] == (Oad{0x0020, 2, 0}));
    }

    SECTION("OAD 与 ROAD 编码互不混淆") {
        // 同样的 OAD 作为普通列与作为 ROAD 主列时字节不同。
        Writer plain_writer{32};
        codec::write_rcsd(plain_writer, Rcsd{Csd{Oad{0x5004, 2, 0}}}, Limits{});
        const Bytes plain = plain_writer.take();
        Writer road_writer{32};
        codec::write_rcsd(road_writer, Rcsd{Csd{Road{Oad{0x5004, 2, 0}, {Oad{0x1002, 2, 0}}}}},
                          Limits{});
        const Bytes with_road = road_writer.take();
        CHECK(to_hex(ByteView{plain}) == "01 00 50 04 02 00");
        // ROAD 分支标签为 01，其后是主 OAD、关联个数与各关联 OAD。
        CHECK(to_hex(ByteView{with_road}) == "01 01 50 04 02 00 01 10 02 02 00");
    }
}

TEST_CASE("MS 表计选择覆盖全部 CHOICE 分支", "[codec][record]") {
    // get_record_meters_request.hex 使用 TSA 集合分支，共五块表计。
    const Bytes bytes = fixture("get_record_meters_request.hex");
    auto decoded = apdu::decode_get(bytes);
    REQUIRE(static_cast<bool>(decoded));
    const auto* request = std::get_if<apdu::GetRecordRequest>(&decoded.value());
    REQUIRE(request != nullptr);
    const auto* selector = std::get_if<Selector5>(&request->records.front().rows);
    REQUIRE(selector != nullptr);
    const auto* meters = std::get_if<MeterAddresses>(&selector->meters);
    REQUIRE(meters != nullptr);
    REQUIRE(meters->values.size() == 5);
    // 五个表计地址长度一致，均为描述字节加五字节地址。
    for (const auto& address : meters->values) CHECK(address.value.size() == 6);
    // 末字节互不相同，说明集合保留了顺序而非去重排序。
    for (std::size_t i = 1; i < meters->values.size(); ++i)
        CHECK(meters->values[i].value.back() == static_cast<std::uint8_t>(0x21 + i));
}

TEST_CASE("RSD 各分支独立往返", "[codec][record]") {
    // 注意：RSD 内部的 date_time_s 不重复携带 0x1C 标签，省去每处一个字节。
    const Oad target{0x5004, 2, 0};
    const DateTimeS begin{{0x07, 0xe0, 1, 20, 0, 0, 0}};
    const DateTimeS end{{0x07, 0xe0, 2, 20, 0, 0, 0}};
    const Ti interval{2, 15};
    const Ms all = AllMeters{};

    expect_rsd("00", SelectAll{});
    expect_rsd("01 50 04 02 00 1C 07 E0 01 14 00 00 00", Selector1{target, DateTimeS{begin}});
    expect_rsd("02 50 04 02 00 1C 07 E0 01 14 00 00 00 1C 07 E0 02 14 00 00 00 54 02 00 0F",
               Selector2{target, DateTimeS{begin}, DateTimeS{end}, interval});
    expect_rsd("04 07 E0 01 14 00 00 00 01", Selector4{DateTimeS{begin}, all});
    expect_rsd("05 07 E0 01 14 00 00 00 01", Selector5{DateTimeS{begin}, all});
    expect_rsd("06 07 E0 01 14 00 00 00 07 E0 02 14 00 00 00 02 00 0F 01",
               Selector6{DateTimeS{begin}, DateTimeS{end}, interval, all});
    expect_rsd("09 03", Selector9{3});
    expect_rsd("0A 02 01", Selector10{2, all});
}

TEST_CASE("RSD 分支编号越界被拒绝", "[codec][record]") {
    // 选择器编号只到 10（0x0A），更大的编号不是合法 CHOICE 分支。
    const Bytes out_of_range = hex("0B 00");
    Reader reader{ByteView{out_of_range}};
    CHECK_DECODE_ERROR(codec::read_rsd(reader), ErrorCode::invalid_value, "RSD choice");
}

TEST_CASE("MS 分支编号与内容一致", "[codec][record]") {
    SECTION("无表计与全部表计都是单字节分支") {
        const Bytes none_bytes = hex("00");
        Reader none_reader{ByteView{none_bytes}};
        CHECK(codec::read_ms(none_reader) == Ms{NoMeters{}});

        const Bytes all_bytes = hex("01");
        Reader all_reader{ByteView{all_bytes}};
        CHECK(codec::read_ms(all_reader) == Ms{AllMeters{}});
    }

    SECTION("表计类型集合保留顺序") {
        const Bytes bytes = hex("02 03 01 02 03");
        Reader reader{ByteView{bytes}};
        const Ms value = codec::read_ms(reader);
        const auto* types = std::get_if<MeterTypes>(&value);
        REQUIRE(types != nullptr);
        CHECK(types->values == std::vector<std::uint8_t>{1, 2, 3});
        Writer writer{16};
        codec::write_ms(writer, value, Limits{});
        const Bytes encoded = writer.take();
        CHECK(to_hex(ByteView{encoded}) == "02 03 01 02 03");
    }

    SECTION("表计号集合按大端编码") {
        const Bytes bytes = hex("04 02 00 01 00 02");
        Reader reader{ByteView{bytes}};
        const Ms value = codec::read_ms(reader);
        const auto* numbers = std::get_if<MeterNumbers>(&value);
        REQUIRE(numbers != nullptr);
        CHECK(numbers->values == std::vector<std::uint16_t>{1, 2});
    }
}

TEST_CASE("记录描述符遵守资源限制", "[codec][record]") {
    SECTION("RCSD 列数上限") {
        // 每个 OAD 列占 4 字节，元素上限设为 2 时第三列必须被拒绝。
        const Bytes three_columns = hex("03 00 40 01 02 00 00 40 01 03 00 00 40 01 04 00");
        Reader reader{ByteView{three_columns}};
        Limits limits;
        limits.max_elements = 2;
        CHECK_DECODE_ERROR(codec::read_rcsd(reader, limits), ErrorCode::resource_limit);
    }

    SECTION("上限放宽后放行") {
        const Bytes three_columns = hex("03 00 40 01 02 00 00 40 01 03 00 00 40 01 04 00");
        Reader reader{ByteView{three_columns}};
        Limits limits;
        limits.max_elements = 3;
        CHECK(codec::read_rcsd(reader, limits).size() == 3);
    }
}

TEST_CASE("记录响应向量按规范字节往返", "[codec][record]") {
    const Bytes bytes = fixture("get_record_response.hex");
    auto decoded = apdu::decode_get(bytes);
    REQUIRE(static_cast<bool>(decoded));
    auto encoded = apdu::encode_get(decoded.value());
    REQUIRE(static_cast<bool>(encoded));
    CHECK(to_hex(ByteView{encoded.value()}) == to_hex(ByteView{bytes}));

    const auto* response = std::get_if<apdu::GetRecordResponse>(&decoded.value());
    REQUIRE(response != nullptr);
    // 线上首字节 0x85 的 ACD 位在解码时剥离，PIID 部分为 3。
    CHECK(response->piid_acd == 0x03);
    REQUIRE(response->records.size() == 1);
    const auto& record = response->records.front();
    CHECK(record.attribute == (Oad{0x5004, 2, 0}));
    REQUIRE(record.columns.size() == 2);
    const auto* rows = std::get_if<std::vector<apdu::RecordRow>>(&record.result);
    REQUIRE(rows != nullptr);
    REQUIRE(rows->size() == 1);
    // 首列为 date_time_s，次列为 array/UInt32，含总及四个费率的五个零值。
    REQUIRE(rows->front().size() == 2);
    const auto& stamp = std::get<DateTimeS>(rows->front()[0].payload);
    CHECK(stamp.value[1] == 0xe0);
    const auto& totals = std::get<Array>(rows->front()[1].payload);
    REQUIRE(totals.value.size() == 5);
    for (const auto& item : totals.value) CHECK(std::get<UInt32>(item.payload).value == 0);

    dlt698::test::require_truncation_rejected(
        [](ByteView input) { return apdu::decode_get(input); }, ByteView{bytes});
}

TEST_CASE("记录结果可为 DAR 而非数据行", "[codec][record]") {
    // 记录响应标签 85，其中结果 CHOICE 选 00 表示单个 DAR 字节。
    // 这里逐字段构造：D.3.3 响应结构去掉数据行，改用 DAR=4（未定义）。
    apdu::GetRecordResponse model;
    model.piid_acd = 0x03;
    model.list = false;
    apdu::RecordResult item;
    item.attribute = Oad{0x5004, 2, 0};
    item.columns = Rcsd{Csd{Oad{0x2021, 2, 0}}};
    item.result = std::uint8_t{4};
    model.records.push_back(std::move(item));

    auto encoded = apdu::encode_get(apdu::GetApdu{model});
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();

    auto decoded = apdu::decode_get(bytes);
    REQUIRE(static_cast<bool>(decoded));
    const auto* response = std::get_if<apdu::GetRecordResponse>(&decoded.value());
    REQUIRE(response != nullptr);
    REQUIRE(response->records.size() == 1);
    const auto* code = std::get_if<std::uint8_t>(&response->records.front().result);
    REQUIRE(code != nullptr);
    CHECK(*code == 4);

    SECTION("外层 Result 成功但逐项结果是 DAR") {
        // 这是文档反复强调的陷阱：不能只看 decode_get 是否成功就当作业务成功。
        CHECK(static_cast<bool>(decoded));
    }
}