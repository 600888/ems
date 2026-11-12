/**
 * @file apdu_test.cpp
 * @brief 统一 APDU 路由的单元测试：服务标签分派与资源上限。
 */
#include <dlt698/protocol/apdu/apdu.hpp>

#include "catch/test_support.hpp"

using namespace dlt698;
using namespace dlt698::model;
using namespace dlt698::protocol::apdu;
using dlt698::test::fixture;
using dlt698::test::hex;

namespace {

/**
 * @brief 断言解码得到的变体下标与预期消息一致。
 * @param[in] bytes 待解码的 APDU 字节。
 * @param[in] expected_index 预期的 Apdu 变体下标。
 * @note Apdu 变体顺序为LINK、CONNECT、GET、SET、ACTION、记录与 Next，见 apdu.hpp。
 */
void expect_variant(const Bytes& bytes, std::size_t expected_index) {
    INFO("字节=" << to_hex(ByteView{bytes}));
    auto decoded = decode_apdu(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    CHECK(decoded.value().index() == expected_index);
}

/** @brief 构造最小可编码的各类型消息，用于验证路由与往返。 */
struct SampleMessages {
    LinkRequest link_request;
    LinkResponse link_response;
    ConnectRequest connect_request;
    ConnectResponse connect_response;
    ReleaseRequest release_request;
    ReleaseResponse release_response;
    ReleaseNotification release_notification;
    ErrorResponse error_response;
    GetRequest get_request;
    GetResponse get_response;
    GetRecordRequest get_record_request;
    GetRecordResponse get_record_response;
    GetNextRequest get_next_request;
    GetNextResponse get_next_response;
    SetRequest set_request;
    SetResponse set_response;
    ActionRequest action_request;
    ActionResponse action_response;
};

/**
 * @brief 构造各类型的最小可编码消息。
 * @return 覆盖全部十八个变体的消息集合。
 * @note 时间与厂商字段取规范附录中的示例值，便于失败时人工核对。
 */
SampleMessages make_samples() {
    SampleMessages s;
    s.link_request.type = LinkRequestType::login;
    s.link_request.heartbeat_seconds = 180;
    s.link_request.requested_at =
        DateTime{{0x07, 0xe0, 0x05, 0x13, 0x04, 0x08, 0x05, 0x00, 0x00, 0xa4}};
    s.link_response.result = 0x80;
    s.link_response.requested_at = s.link_request.requested_at;
    s.link_response.received_at = s.link_request.requested_at;
    s.link_response.responded_at = s.link_request.requested_at;
    s.connect_request.parameters.protocol.fill(0xff);
    s.connect_request.parameters.function.fill(0xff);
    s.connect_response.factory.manufacturer = {'T', 'O', 'P', 'S'};
    s.connect_response.parameters.protocol.fill(0xff);
    s.connect_response.parameters.function.fill(0xff);
    s.release_request.piid = 1;
    s.release_response.piid_acd = 1;
    s.release_notification.established_at = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};
    s.release_notification.current_time = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x00}};
    s.error_response.piid = 3;
    s.error_response.type = 2;
    s.get_request.piid = 1;
    s.get_request.attributes = {Oad{0x4001, 0x02, 0x00}};
    s.get_response.piid_acd = 1;
    s.get_response.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00}, Null{}}};
    s.get_record_request.piid = 1;
    s.get_record_request.records = {GetRecord{Oad{0x5004, 0x02, 0x00}, SelectAll{}, {}}};
    s.get_record_response.piid_acd = 1;
    s.get_record_response.records = {RecordResult{Oad{0x5004, 0x02, 0x00}, {}, std::uint8_t{0}}};
    s.get_next_request.piid = 1;
    s.get_next_request.block = 0;
    s.get_next_response.piid_acd = 1;
    s.get_next_response.last = true;
    s.get_next_response.result =
        std::vector<AttributeResult>{AttributeResult{Oad{0x4001, 0x02, 0x00}, Null{}}};
    s.set_request.piid = 1;
    s.set_request.attributes = {SetAttribute{Oad{0x4001, 0x02, 0x00}, Null{}}};
    s.set_response.piid_acd = 1;
    s.set_response.attributes = {SetResult{Oad{0x4001, 0x02, 0x00}, 0}};
    s.action_request.piid = 1;
    s.action_request.methods = {ActionMethod{Omd{0x0010, 0x01, 0x00}, Null{}}};
    s.action_response.piid_acd = 1;
    s.action_response.methods = {ActionResult{Omd{0x0010, 0x01, 0x00}, 0, std::nullopt}};
    return s;
}

}  // namespace

TEST_CASE("APDU 路由按服务标签分派到精确类型", "[apdu][routing]") {
    const SampleMessages samples = make_samples();

    SECTION("连接类标签") {
        struct Case {
            const char* label;
            Apdu message;
            std::uint8_t service;
            std::size_t index;
        };

        const Case cases[] = {{"LinkRequest", Apdu{samples.link_request}, 0x01, 0},
                              {"LinkResponse", Apdu{samples.link_response}, 0x81, 1},
                              {"ConnectRequest", Apdu{samples.connect_request}, 0x02, 2},
                              {"ConnectResponse", Apdu{samples.connect_response}, 0x82, 3},
                              {"ReleaseRequest", Apdu{samples.release_request}, 0x03, 4},
                              {"ReleaseResponse", Apdu{samples.release_response}, 0x83, 5},
                              {"ReleaseNotification", Apdu{samples.release_notification}, 0x84, 6},
                              {"ErrorResponse", Apdu{samples.error_response}, 0xee, 7}};
        for (const auto& c : cases) {
            INFO("类型=" << c.label);
            auto encoded = encode_apdu(c.message);
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            // 首字节就是服务标签，路由完全由它决定。
            CHECK(bytes[0] == c.service);
            expect_variant(bytes, c.index);
            // 变体下标必须与消息本身一致，避免"编成A 类、解成B 类"。
            CHECK(c.message.index() == c.index);
        }
    }

    SECTION("GET 家族标签") {
        struct Case {
            const char* label;
            Apdu message;
            std::uint8_t service;
            std::size_t index;
        };

        const Case cases[] = {{"GetRequest", Apdu{samples.get_request}, 0x05, 8},
                              {"GetResponse", Apdu{samples.get_response}, 0x85, 9},
                              {"GetRecordRequest", Apdu{samples.get_record_request}, 0x05, 14},
                              {"GetRecordResponse", Apdu{samples.get_record_response}, 0x85, 15},
                              {"GetNextRequest", Apdu{samples.get_next_request}, 0x05, 16},
                              {"GetNextResponse", Apdu{samples.get_next_response}, 0x85, 17}};
        for (const auto& c : cases) {
            INFO("类型=" << c.label);
            auto encoded = encode_apdu(c.message);
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            CHECK(bytes[0] == c.service);
            // 请求与响应共用同一服务标签，由第二字节的变体号区分。
            expect_variant(bytes, c.index);
            CHECK(c.message.index() == c.index);
        }
    }

    SECTION("SET 与 ACTION 家族标签") {
        struct Case {
            const char* label;
            Apdu message;
            std::uint8_t service;
            std::size_t index;
        };

        const Case cases[] = {{"SetRequest", Apdu{samples.set_request}, 0x06, 10},
                              {"SetResponse", Apdu{samples.set_response}, 0x86, 11},
                              {"ActionRequest", Apdu{samples.action_request}, 0x07, 12},
                              {"ActionResponse", Apdu{samples.action_response}, 0x87, 13}};
        for (const auto& c : cases) {
            INFO("类型=" << c.label);
            auto encoded = encode_apdu(c.message);
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            CHECK(bytes[0] == c.service);
            expect_variant(bytes, c.index);
            CHECK(c.message.index() == c.index);
        }
    }

    SECTION("每个类型再编码都得到同一串字节") {
        const Apdu messages[] = {
            Apdu{samples.link_request},         Apdu{samples.link_response},
            Apdu{samples.connect_request},      Apdu{samples.connect_response},
            Apdu{samples.release_request},      Apdu{samples.release_response},
            Apdu{samples.release_notification}, Apdu{samples.error_response},
            Apdu{samples.get_request},          Apdu{samples.get_response},
            Apdu{samples.get_record_request},   Apdu{samples.get_record_response},
            Apdu{samples.get_next_request},     Apdu{samples.get_next_response},
            Apdu{samples.set_request},          Apdu{samples.set_response},
            Apdu{samples.action_request},       Apdu{samples.action_response}};
        for (std::size_t i = 0; i < std::size(messages); ++i) {
            INFO("变体下标=" << i);
            auto first = encode_apdu(messages[i]);
            REQUIRE(static_cast<bool>(first));
            const Bytes bytes = first.value();
            auto decoded = decode_apdu(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            auto second = encode_apdu(decoded.value());
            REQUIRE(static_cast<bool>(second));
            const Bytes result = second.value();
            CHECK(to_hex(ByteView{result}) == to_hex(ByteView{bytes}));
        }
    }
}

TEST_CASE("APDU 路由在规范向量上分派正确", "[apdu][routing]") {
    struct Case {
        const char* file;
        std::size_t index;
    };

    const Case cases[] = {{"link-login-request.hex", 0},
                          {"link-login-response.hex", 1},
                          {"connect-request.hex", 2},
                          {"connect-response.hex", 3},
                          {"release-request.hex", 4},
                          {"release-response.hex", 5},
                          {"release-notification.hex", 6},
                          {"error-response.hex", 7},
                          {"get-normal-request.hex", 8},
                          {"get-normal-response.hex", 9},
                          {"set-normal-request.hex", 10},
                          {"set-normal-response.hex", 11},
                          {"action-normal-request.hex", 12},
                          {"action-normal-response.hex", 13},
                          {"get_record_request.hex", 14},
                          {"get_record_response.hex", 15},
                          {"get-list-request.hex", 8},
                          {"get-list-response.hex", 9},
                          {"get_record_meters_request.hex", 14},
                          {"set-list-request.hex", 10},
                          {"set-list-response.hex", 11}};

    for (const auto& c : cases) {
        INFO("向量=" << c.file);
        const Bytes bytes = fixture(c.file);
        auto decoded = decode_apdu(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK(decoded.value().index() == c.index);

        // 统一入口再编码必须与规范向量逐字节一致。
        auto encoded = encode_apdu(decoded.value());
        REQUIRE(static_cast<bool>(encoded));
        const Bytes result = encoded.value();
        CHECK(to_hex(ByteView{result}) == to_hex(ByteView{bytes}));
    }
}

TEST_CASE("未分配的服务标签报不支持", "[apdu][routing]") {
    const auto decode = [](ByteView input, const Limits& limits) {
        return decode_apdu(input, limits);
    };

    SECTION("连接类未分配标签") {
        // 0x04、0x08、0x09 等未分配给任何服务，路由不得猜测。
        for (const char* text : {"00 00", "04 00", "08 00", "09 00", "0A 00", "FF 00", "F0 00"}) {
            INFO("输入=" << text);
            const Bytes bytes = hex(text);
            auto result = decode(ByteView{bytes}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
        }
    }

    SECTION("空输入报告需要更多数据") {
        const Bytes empty;
        auto result = decode(ByteView{empty}, Limits{});
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::need_more_data);
        CHECK(result.error().context == "APDU service");
    }

    SECTION("只有服务标签一个字节时报告需要更多数据") {
        for (const char* text : {"05", "06", "02", "EE"}) {
            INFO("输入=" << text);
            const Bytes bytes = hex(text);
            auto result = decode(ByteView{bytes}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::need_more_data);
        }
    }

    SECTION("GET 与 SET 家族的未分配变体") {
        // GET 变体 0 与 7 及以上未分配。
        for (int choice : {0, 7, 8, 9}) {
            INFO("GET 变体=" << choice);
            Bytes bytes{0x05, static_cast<std::uint8_t>(choice), 0x01};
            bytes.insert(bytes.end(), {0x40, 0x01, 0x02, 0x00, 0x00, 0x00});
            auto result = decode(ByteView{bytes}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::unsupported_service);
        }
        // SET 与 ACTION 变体 0 与 4 及以上未分配。
        for (std::uint8_t service : {0x06, 0x07, 0x86, 0x87}) {
            for (int choice : {0, 4}) {
                INFO("服务=" << std::hex << +service << std::dec << " 变体=" << choice);
                Bytes bytes{service, static_cast<std::uint8_t>(choice), 0x01};
                bytes.insert(bytes.end(), {0x40, 0x01, 0x02, 0x00, 0x00, 0x00});
                auto result = decode(ByteView{bytes}, Limits{});
                REQUIRE_FALSE(static_cast<bool>(result));
                CHECK(result.error().code == ErrorCode::unsupported_service);
            }
        }
    }
}

TEST_CASE("APDU 逐字节截断全部拒绝", "[apdu][routing]") {
    const char* names[] = {
        "link-login-request.hex",   "link-login-response.hex",   "connect-request.hex",
        "connect-response.hex",     "release-request.hex",       "release-response.hex",
        "release-notification.hex", "error-response.hex",        "get-normal-request.hex",
        "get-normal-response.hex",  "get-list-request.hex",      "get-list-response.hex",
        "get_record_request.hex",   "get_record_response.hex",   "get_record_meters_request.hex",
        "set-normal-request.hex",   "set-normal-response.hex",   "set-list-request.hex",
        "set-list-response.hex",    "action-normal-request.hex", "action-normal-response.hex"};

    for (const char* name : names) {
        INFO("向量=" << name);
        const Bytes bytes = fixture(name);
        // 半个协议字段必须报 need_more_data，而不是被当作损坏数据静默丢弃。
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            INFO("截断到 " << n << " 字节");
            CHECK_FALSE(static_cast<bool>(decode_apdu(ByteView{bytes}.subview(0, n), Limits{})));
        }
        CHECK(static_cast<bool>(decode_apdu(ByteView{bytes}, Limits{})));
    }
}

TEST_CASE("完整 APDU 之后的多余字节报尾随数据", "[apdu][routing]") {
    SECTION("各家族向量追加一个零字节都被拒绝") {
        const char* names[] = {"link-login-request.hex", "connect-request.hex",
                               "release-request.hex",    "error-response.hex",
                               "get-normal-request.hex", "get-normal-response.hex",
                               "set-normal-request.hex", "action-normal-response.hex"};
        for (const char* name : names) {
            INFO("向量=" << name);
            const Bytes bytes = fixture(name);
            Bytes padded = bytes;
            padded.push_back(0x00);
            auto result = decode_apdu(ByteView{padded}, Limits{});
            REQUIRE_FALSE(static_cast<bool>(result));
            CHECK(result.error().code == ErrorCode::trailing_data);
        }
    }

    SECTION("追加多个零字节同样被拒绝") {
        const Bytes bytes = fixture("release-request.hex");
        Bytes padded = bytes;
        padded.insert(padded.end(), {0x00, 0x00, 0x00});
        auto result = decode_apdu(ByteView{padded}, Limits{});
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::trailing_data);
        CHECK(result.error().context == "trailing bytes");
    }

    SECTION("服务器响应尾部的FollowReport 位置参与尾随判定") {
        // 把FollowReport 与时间标签两个标记都改成非零值会被单独识别，
        // 因此这里只验证纯多余字节的情形，避免与 FollowReport 错误混淆。
        const Bytes bytes = fixture("release-response.hex");
        CHECK(bytes.size() == 5);
        Bytes padded = bytes;
        padded.push_back(0x00);
        padded.push_back(0x00);
        auto result = decode_apdu(ByteView{padded}, Limits{});
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::trailing_data);
    }
}

TEST_CASE("APDU 编解码遵守资源上限", "[apdu][routing]") {
    const auto decode = [](ByteView input, const Limits& limits) {
        return decode_apdu(input, limits);
    };

    SECTION("输入字节上限在连接类生效") {
        const Bytes bytes = fixture("connect-response.hex");
        Limits limits;
        limits.max_data_bytes = 32;
        auto result = decode(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::resource_limit);
        CHECK(result.error().context == "APDU input limit");
        // 恰好等于上限时放行。
        limits.max_data_bytes = bytes.size();
        CHECK(static_cast<bool>(decode(ByteView{bytes}, limits)));
    }

    SECTION("输入字节上限在 GET 类生效") {
        const Bytes bytes = fixture("get-list-response.hex");
        Limits limits;
        limits.max_data_bytes = 16;
        auto result = decode(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::resource_limit);
        CHECK(result.error().context == "GET APDU bytes");
    }

    SECTION("输入字节上限在 SET 与 ACTION 类生效") {
        const Bytes bytes = fixture("set-list-request.hex");
        Limits limits;
        limits.max_data_bytes = 16;
        auto result = decode(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(result));
        CHECK(result.error().code == ErrorCode::resource_limit);
        CHECK(result.error().context == "APDU limit");
    }

    SECTION("输出字节上限在三个家族都生效") {
        const SampleMessages samples = make_samples();
        const Apdu messages[] = {Apdu{samples.connect_request}, Apdu{samples.get_request},
                                 Apdu{samples.set_request}};
        for (const auto& message : messages) {
            INFO("变体下标=" << message.index());
            // GET 普通请求恰好八字节，因此预算须小于八才能对三个家族同时生效。
            Limits limits;
            limits.max_data_bytes = 7;
            auto encoded = encode_apdu(message, limits);
            REQUIRE_FALSE(static_cast<bool>(encoded));
            CHECK(encoded.error().code == ErrorCode::resource_limit);
            CHECK(encoded.error().context == "output limit");
        }
        // 预算放宽到八字节后最小的 GET 请求恰好装满。
        Limits relaxed;
        relaxed.max_data_bytes = 8;
        CHECK(static_cast<bool>(encode_apdu(Apdu{samples.get_request}, relaxed)));
    }

    SECTION("元素数上限只影响列表形态") {
        const SampleMessages samples = make_samples();
        // GET 列表请求声明两项，元素上限为一时必须被拒绝。
        GetRequest list;
        list.piid = 1;
        list.list = true;
        list.attributes = {Oad{0x2000, 0x02, 0x00}, Oad{0x2001, 0x02, 0x00}};
        Limits limits;
        limits.max_elements = 1;
        auto rejected = encode_apdu(Apdu{list}, limits);
        REQUIRE_FALSE(static_cast<bool>(rejected));
        CHECK(rejected.error().code == ErrorCode::invalid_length);

        // 普通形态只有一项，与元素上限无关。
        CHECK(static_cast<bool>(encode_apdu(Apdu{samples.get_request}, limits)));
    }

    SECTION("Data 树深度上限逐层生效") {
        const Data inner{Array{{Data{UInt8{1}}}}};
        const Data middle{Array{{inner}}};
        const Data outer{Array{{middle}}};
        GetResponse response;
        response.piid_acd = 1;
        response.attributes = {AttributeResult{Oad{0x4001, 0x02, 0x00}, outer}};
        auto encoded = encode_apdu(Apdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();

        Limits limits;
        limits.max_depth = 2;
        auto too_deep = decode(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(too_deep));
        CHECK(too_deep.error().code == ErrorCode::resource_limit);
        limits.max_depth = 32;
        CHECK(static_cast<bool>(decode(ByteView{bytes}, limits)));
    }

    SECTION("统一入口与家族入口遵守同一上限") {
        // 路由只是转发，因此两个入口在同一预算下必须给出相同结论。
        const Bytes bytes = fixture("get-normal-response.hex");
        Limits limits;
        limits.max_data_bytes = 4;
        auto via_router = decode(ByteView{bytes}, limits);
        auto via_family = decode_get(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(via_router));
        REQUIRE_FALSE(static_cast<bool>(via_family));
        CHECK(via_router.error().code == via_family.error().code);
        CHECK(via_router.error().context == via_family.error().context);
    }
}