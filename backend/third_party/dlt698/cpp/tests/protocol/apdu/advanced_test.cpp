#include <dlt698/dlt698.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::protocol::apdu;
using namespace dlt698::model;

namespace {
void vector_check(const char* text) {
    const auto bytes = test::hex(text);
    const auto decoded = decode_apdu(bytes);
    REQUIRE(decoded);
    const auto encoded = encode_apdu(decoded.value());
    REQUIRE(encoded);
    CHECK(encoded.value() == bytes);
    test::require_truncation_rejected([](ByteView b) { return decode_apdu(b); }, bytes);
    auto trailing = bytes;
    trailing.push_back(0);
    CHECK_FALSE(decode_apdu(trailing));
}
}  // namespace

TEST_CASE("ThenGet 与 PROXY 使用标准附录 D 的独立字节向量", "[advanced][vectors]") {
    vector_check("06 03 04 01 45 00 02 0C 12 01 68 45 00 02 0C 03 00");
    vector_check("86 03 04 01 45 00 02 0C 00 45 00 02 0C 01 12 01 68 00 00");
    vector_check("07 03 07 01 00 10 01 00 0F 00 00 10 02 00 00 00");
    vector_check(
        "87 03 07 01 00 10 01 00 00 00 00 10 02 00 01 01 05 06 00 00 00 00 06 00 00 00 00 06 00 00 "
        "00 00 06 00 00 00 00 06 00 00 00 00 00 00");
    vector_check(
        "09 01 0A 00 78 02 07 05 20 16 01 20 00 01 00 3C 01 00 10 02 00 07 05 20 16 01 20 00 02 00 "
        "3C 01 00 10 02 00 00");
    vector_check(
        "89 01 0A 02 07 05 20 16 01 20 00 01 01 00 10 02 00 01 01 05 06 00 00 00 00 06 00 00 00 00 "
        "06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 07 05 20 16 01 20 00 02 01 00 10 02 00 01 01 "
        "05 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 06 00 00 00 00 00 00");
}

TEST_CASE("REPORT 三类通知确认与 FollowReport 两类尾部独立格式", "[advanced][report]") {
    vector_check("88 01 42 01 20 00 02 00 01 12 00 DC 00 00");
    vector_check("08 01 02 01 20 00 02 00 00");
    vector_check("88 02 02 01 50 04 02 00 01 00 20 00 02 00 01 01 12 00 DC 00 00");
    vector_check("08 02 02 01 50 04 02 00 00");
    vector_check("88 03 02 F2 01 02 00 02 01 AA 02 BB CC 00 00");
    vector_check("08 03 02 00");
    vector_check("85 01 42 20 00 02 00 01 12 00 DC 01 01 01 20 01 02 00 00 04 00");
    vector_check(
        "86 01 02 20 00 02 00 00 01 02 01 50 04 02 00 01 00 20 00 02 00 01 01 12 00 DC 00");
}

TEST_CASE("PROXY 七类请求响应字段及可选 ACTION Data", "[advanced][proxy]") {
    for (const auto text :
         {"09 02 02 00 0A 02 00 01 50 04 02 00 00 01 00 20 00 02 00 00",
          "89 02 02 02 00 01 50 04 02 00 01 00 20 00 02 00 00 04 00 00",
          "09 03 02 00 0A 01 02 00 01 00 03 01 20 00 02 00 12 00 DC 00",
          "89 03 02 01 02 00 01 01 20 00 02 00 00 00 00",
          "09 04 02 00 0A 01 02 00 01 00 03 01 20 00 02 00 12 00 DC 20 00 02 00 01 00",
          "89 04 02 01 02 00 01 01 20 00 02 00 00 20 00 02 00 01 12 00 DC 00 00",
          "09 05 02 00 0A 01 02 00 01 00 03 01 00 10 01 00 00 00",
          "89 05 02 01 02 00 01 01 00 10 01 00 00 01 00 00 00",
          "09 06 02 00 0A 01 02 00 01 00 03 01 00 10 01 00 00 20 00 02 00 01 00",
          "89 06 02 01 02 00 01 01 00 10 01 00 00 00 20 00 02 00 00 04 00 00",
          "09 07 02 F2 01 02 00 06 02 08 01 00 00 03 00 0A 02 AA BB 00",
          "89 07 02 F2 01 02 00 01 02 AA BB 00 00", "89 07 02 F2 01 02 00 00 02 00 00"})
        vector_check(text);
}

TEST_CASE("安全 Data 与 SECURITY 全分支固定字节及截断拒绝", "[advanced][security]") {
    for (const auto text : {"56 02 AA BB", "57 02 01 02", "5D 00 00 00 01 01 AA",
                            "5E 00 00 00 01 01 AA 02 BB CC", "5F 06 02 08 01 00"}) {
        const auto bytes = test::hex(text);
        const auto value = codec::decode_data(bytes);
        REQUIRE(value);
        REQUIRE(codec::encode_data(value.value()));
        CHECK(codec::encode_data(value.value()).value() == bytes);
        test::require_truncation_rejected([](ByteView b) { return codec::decode_data(b); }, bytes);
    }
    for (const auto text :
         {"10 00 02 05 00 00 00 00 00 01 01 AA 01 BB", "10 01 01 AA 01 02 BB CC",
          "10 00 01 AA 02 01 BB 01 CC", "10 01 01 AA 03 00 00 00 01 00", "90 00 02 85 00 00",
          "90 01 01 AA 01 00 01 BB", "90 00 01 AA 01 01 00 00 00 01 01 BB 01 CC", "90 02 02 00"}) {
        const auto bytes = test::hex(text);
        const auto value = decode_security(bytes);
        REQUIRE(value);
        const auto encoded = encode_security(value.value());
        REQUIRE(encoded);
        CHECK(encoded.value() == bytes);
        test::require_truncation_rejected([](ByteView b) { return decode_security(b); }, bytes);
        auto trailing = bytes;
        trailing.push_back(0);
        CHECK_FALSE(decode_security(trailing));
    }
}

TEST_CASE("高级消息严格拒绝非法控制块零代理超时及资源超限", "[advanced][limits]") {
    for (const auto text : {"09 01 01 00 00 01 02 00 01 00 03 01 20 00 02 00 00",
                            "09 07 02 F2 01 02 00 0B 02 08 01 00 00 03 00 0A 02 AA BB 00",
                            "88 01 01 00 00 00", "08 04 01 00", "10 02 00", "90 03 00",
                            "09 01 02 00 0A 01 02 80 01 00 03 01 20 00 02 00 00"}) {
        const auto bytes = test::hex(text);
        const bool accepted = bytes[0] == 0x10 || bytes[0] == 0x90
                                  ? static_cast<bool>(decode_security(bytes))
                                  : static_cast<bool>(decode_apdu(bytes));
        CHECK_FALSE(accepted);
    }
    auto bytes = test::hex(
        "09 01 0A 00 78 02 02 00 01 00 3C 01 00 10 02 00 02 00 02 00 3C 01 00 10 02 00 00");
    Limits l;
    l.max_elements = 1;
    CHECK_FALSE(decode_apdu(bytes, l));
}

TEST_CASE("GET MD5 使用固定摘要长度及 RFC 1321 算法向量", "[advanced][md5]") {
    vector_check("05 06 01 40 01 02 00 00");
    vector_check(
        "85 06 01 40 01 02 00 01 10 D4 1D 8C D9 8F 00 B2 04 E9 80 09 98 EC F8 42 7E 00 00");
    for (const auto& v : std::vector<std::pair<std::string, std::string>>{
             {"", "d41d8cd98f00b204e9800998ecf8427e"},
             {"abc", "900150983cd24fb0d6963f7d28e17f72"},
             {std::string(56, 'a'), "3b0c8ac703f828b04c6c197006d17218"},
             {std::string(1000000, 'a'), "7707d6ae4e027c70eea2a935c2296f21"}}) {
        const Bytes bytes(v.first.begin(), v.first.end());
        const auto digest = md5(bytes);
        const auto expected = test::hex(v.second);
        CHECK(Bytes(digest.begin(), digest.end()) == expected);
    }
}
