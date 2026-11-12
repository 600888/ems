/**
 * @file connection_test.cpp
 * @brief LINK、CONNECT、RELEASE 与异常响应编解码的单元测试。
 */
#include <dlt698/protocol/apdu/connection.hpp>

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
 * @param[in] message 待校验的连接类消息。
 * @note 向量不由被测编码器生成，因此这是真正的独立比对而非自洽往返。
 */
void expect_vector(const char* expected, const ConnectionApdu& message) {
    INFO("向量=" << expected);
    auto encoded = encode_connection(message);
    REQUIRE(static_cast<bool>(encoded));
    const Bytes bytes = encoded.value();
    const Bytes expected_bytes = fixture(expected);
    CHECK(to_hex(ByteView{bytes}) == to_hex(ByteView{expected_bytes}));

    auto decoded = decode_connection(ByteView{bytes});
    REQUIRE(static_cast<bool>(decoded));
    // 连接类消息没有定义字段级operator==，因此用"变体下标相同且再编码得到同一串字节"
    // 证明解码结果与输入模型完全一致。
    CHECK(decoded.value().index() == message.index());
    auto re_encoded = encode_connection(decoded.value());
    REQUIRE(static_cast<bool>(re_encoded));
    const Bytes result = re_encoded.value();
    CHECK(to_hex(ByteView{result}) == to_hex(ByteView{expected_bytes}));
}

/**
 * @brief 断言连接类 APDU 解码失败，并同时比对错误分类与上下文字段。
 * @param[in] text 十六进制输入文本。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的诊断字段名。
 */
void require_connection_error(const char* text, ErrorCode code, const char* context) {
    INFO("输入=" << text);
    const Bytes bytes = hex(text);
    auto result = decode_connection(ByteView{bytes});
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
    CHECK(result.error().context == context);
}

/// 构造附录 D.1.1 的登录请求：PIID-ACD=0、登录、周期 180 秒。
LinkRequest login_request() {
    LinkRequest request;
    request.piid_acd = 0;
    request.type = LinkRequestType::login;
    request.heartbeat_seconds = 180;
    request.requested_at = DateTime{{0x07, 0xe0, 0x05, 0x13, 0x04, 0x08, 0x05, 0x00, 0x00, 0xa4}};
    return request;
}

/// 构造附录 D.2 的关联参数：版本 0010H、全FF 位图、1024 字节、窗口 1、100 秒。
AssociationParameters all_capability_parameters() {
    AssociationParameters parameters;
    parameters.version = 0x0010;
    parameters.protocol.fill(0xff);
    parameters.function.fill(0xff);
    parameters.send_frame_bytes = 1024;
    parameters.receive_frame_bytes = 1024;
    parameters.receive_window = 1;
    parameters.apdu_bytes = 1024;
    parameters.timeout_seconds = 100;
    return parameters;
}

}  // namespace

TEST_CASE("LINK 登录按附录 D.1 向量往返", "[apdu][connection][link]") {
    expect_vector("link-login-request.hex", ConnectionApdu{login_request()});

    SECTION("请求字段逐项保持") {
        const Bytes bytes = fixture("link-login-request.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& request = std::get<LinkRequest>(decoded.value());
        CHECK(request.piid_acd == 0);
        CHECK(request.type == LinkRequestType::login);
        CHECK(request.heartbeat_seconds == 180);
        // 日历字段按原始字节保存，不做时区换算。
        REQUIRE(request.requested_at.value.size() == 10);
        CHECK(request.requested_at.value[0] == 0x07);
        CHECK(request.requested_at.value[1] == 0xe0);
        CHECK(request.requested_at.value[2] == 0x05);
        CHECK(request.requested_at.value[3] == 0x13);
        CHECK(request.requested_at.value[8] == 0x00);
        CHECK(request.requested_at.value[9] == 0xa4);
    }

    SECTION("登录响应按附录 D.1 向量往返") {
        LinkResponse response;
        response.piid = 0;
        // 结果 80H：bit7 为时钟可信，低三位为零表示成功。
        response.result = 0x80;
        response.requested_at =
            DateTime{{0x07, 0xe0, 0x05, 0x13, 0x04, 0x08, 0x05, 0x00, 0x00, 0x89}};
        response.received_at =
            DateTime{{0x07, 0xe0, 0x05, 0x13, 0x04, 0x08, 0x05, 0x01, 0x02, 0x5f}};
        response.responded_at =
            DateTime{{0x07, 0xe0, 0x05, 0x13, 0x04, 0x08, 0x05, 0x02, 0x02, 0xda}};
        expect_vector("link-login-response.hex", ConnectionApdu{response});
    }

    SECTION("心跳与注销两种请求类型可往返") {
        LinkRequest request = login_request();
        request.type = LinkRequestType::heartbeat;
        request.heartbeat_seconds = 0;
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<LinkRequest>(decoded.value());
        CHECK(back.type == LinkRequestType::heartbeat);
        CHECK(back.heartbeat_seconds == 0);

        request.type = LinkRequestType::logout;
        auto logout = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(logout));
        const Bytes logout_bytes = logout.value();
        auto logout_back = decode_connection(ByteView{logout_bytes});
        REQUIRE(static_cast<bool>(logout_back));
        CHECK(std::get<LinkRequest>(logout_back.value()).type == LinkRequestType::logout);
    }

    SECTION("LINK 结果的保留位与低三位被校验") {
        LinkResponse response;
        response.requested_at = login_request().requested_at;
        response.received_at = response.requested_at;
        response.responded_at = response.requested_at;
        // 低三位取值 0 至 3。
        for (std::uint8_t result : {0x80, 0x81, 0x82, 0x83}) {
            INFO("结果=" << +result);
            response.result = result;
            CHECK(static_cast<bool>(encode_connection(ConnectionApdu{response})));
        }
        // bit3 至 bit6 是保留位。
        for (std::uint8_t result : {0x88, 0x90, 0x08, 0x10}) {
            INFO("结果=" << +result);
            response.result = result;
            CHECK(encode_connection(ConnectionApdu{response}).error().code ==
                  ErrorCode::invalid_value);
        }
        // 低三位超过 3 也是非法取值。
        response.result = 0x84;
        CHECK(encode_connection(ConnectionApdu{response}).error().code == ErrorCode::invalid_value);
    }

    SECTION("LINK 请求类型越界被拒绝") {
        LinkRequest request = login_request();
        request.type = static_cast<LinkRequestType>(3);
        CHECK(encode_connection(ConnectionApdu{request}).error().code == ErrorCode::invalid_value);
        // 解码侧同样拒绝：服务 01、PIID 0、类型 03。
        require_connection_error(
            "01 00 03 00 B4"
            "00 00 00 00 00 00 00 00 00 00",
            ErrorCode::invalid_value, "LINK type");
    }

    SECTION("LINK 消息没有 FollowReport 与 TimeTag 尾部") {
        // 服务 01、PIID 0、登录、周期、心跳时间共 15 字节，末尾没有存在标记。
        const Bytes bytes = fixture("link-login-request.hex");
        CHECK(bytes.size() == 15);
        const Bytes padded = [&] {
            Bytes copy = bytes;
            copy.push_back(0x00);
            return copy;
        }();
        auto decoded = decode_connection(ByteView{padded});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::trailing_data);
    }
}

TEST_CASE("CONNECT 请求按附录 D.2 向量往返", "[apdu][connection][connect]") {
    ConnectRequest request;
    request.piid = 0;
    request.parameters = all_capability_parameters();
    request.mechanism = NullSecurity{};

    expect_vector("connect-request.hex", ConnectionApdu{request});

    SECTION("关联参数字段逐项保持") {
        const Bytes bytes = fixture("connect-request.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ConnectRequest>(decoded.value());
        CHECK(back.piid == 0);
        CHECK(back.parameters.version == 0x0010);
        // 位图按最高位对应序号零，全 FF 表示全部支持。
        for (const auto byte : back.parameters.protocol) CHECK(byte == 0xff);
        for (const auto byte : back.parameters.function) CHECK(byte == 0xff);
        CHECK(back.parameters.send_frame_bytes == 1024);
        CHECK(back.parameters.receive_frame_bytes == 1024);
        CHECK(back.parameters.receive_window == 1);
        CHECK(back.parameters.apdu_bytes == 1024);
        CHECK(back.parameters.timeout_seconds == 100);
        CHECK(back.mechanism.index() == 0);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("关联参数各字段可独立取值") {
        request.parameters.version = 0x0010;
        request.parameters.receive_window = 0;  // 窗口为零表示不启用流水线。
        request.parameters.send_frame_bytes = 512;
        request.parameters.receive_frame_bytes = 2048;
        request.parameters.apdu_bytes = 256;
        request.parameters.timeout_seconds = 0xffffffffu;
        request.parameters.protocol = {0x80, 0, 0, 0, 0, 0, 0, 0};
        request.parameters.function.fill(0);
        request.parameters.function[0] = 0x01;
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ConnectRequest>(decoded.value());
        CHECK(back.parameters.receive_window == 0);
        CHECK(back.parameters.send_frame_bytes == 512);
        CHECK(back.parameters.receive_frame_bytes == 2048);
        CHECK(back.parameters.apdu_bytes == 256);
        CHECK(back.parameters.timeout_seconds == 0xffffffffu);
        CHECK(back.parameters.protocol[0] == 0x80);
        CHECK(back.parameters.protocol[1] == 0);
        CHECK(back.parameters.function[0] == 0x01);
        CHECK(back.parameters.function[1] == 0);
    }

    SECTION("TimeTag 存在与缺失两种形态") {
        const Bytes without = fixture("connect-request.hex");
        auto plain = decode_connection(ByteView{without});
        REQUIRE(static_cast<bool>(plain));
        CHECK_FALSE(std::get<ConnectRequest>(plain.value()).time_tag.has_value());

        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 时间标签净增十个字节：七字节日历、一字节单位与两字节间隔；
        // 不存在的向量尾部已有一个零标记，因此不重复计入。
        CHECK(bytes.size() == without.size() + 10);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& tag = std::get<ConnectRequest>(decoded.value()).time_tag;
        REQUIRE(tag.has_value());
        CHECK(tag->sent_at.value[0] == 0x07);
        CHECK(tag->sent_at.value[3] == 0x13);
        CHECK(tag->allowed_delay.unit == 2);
        CHECK(tag->allowed_delay.interval == 30);
    }

    SECTION("时间间隔单位覆盖 0 至 5") {
        for (std::uint8_t unit = 0; unit <= 5; ++unit) {
            INFO("单位=" << +unit);
            request.time_tag =
                TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {unit, 65535}};
            auto encoded = encode_connection(ConnectionApdu{request});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_connection(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            CHECK(std::get<ConnectRequest>(decoded.value()).time_tag->allowed_delay.unit == unit);
        }
    }

    SECTION("非法单位与非法间隔被拒绝") {
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {6, 1}};
        CHECK(encode_connection(ConnectionApdu{request}).error().code == ErrorCode::invalid_value);
        // 编码后的输入同样被解码侧拒绝：把单位改成 6。
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {2, 30}};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        Bytes bytes = encoded.value();
        bytes[bytes.size() - 3] = 6;
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "TI unit");
    }

    SECTION("时间标签存在标记只能是 0 或 1") {
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        Bytes bytes = encoded.value();
        bytes[bytes.size() - 1] = 2;
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "TimeTag presence");
    }

    SECTION("PIID 保留位必须为零") {
        request.piid = 0x40;
        CHECK(encode_connection(ConnectionApdu{request}).error().code == ErrorCode::invalid_value);
        require_connection_error("02 40 00 10", ErrorCode::invalid_value, "PIID reserved bit");
    }
}

TEST_CASE("SecurityData 四种认证分支往返", "[apdu][connection][connect]") {
    ConnectRequest request;
    request.piid = 0;
    request.parameters = all_capability_parameters();

    SECTION("空认证不携带任何额外字节") {
        request.mechanism = NullSecurity{};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 与附录 D.2 的 NullSecurity 向量逐字节一致。
        const Bytes expected = fixture("connect-request.hex");
        CHECK(to_hex(ByteView{bytes}) == to_hex(ByteView{expected}));
    }

    SECTION("密码认证按可见字符串编码") {
        request.mechanism = PasswordSecurity{"123456"};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 认证选择子 01 后紧跟长度与六个字节。
        const Bytes tail = fixture("connect-request.hex");
        CHECK(bytes.size() == tail.size() + 7);
        CHECK(bytes[39] == 0x01);
        CHECK(bytes[40] == 0x06);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& mechanism = std::get<ConnectRequest>(decoded.value()).mechanism;
        REQUIRE(mechanism.index() == 1);
        CHECK(std::get<PasswordSecurity>(mechanism).password == "123456");
    }

    SECTION("空密码合法") {
        request.mechanism = PasswordSecurity{""};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        CHECK(bytes[40] == 0x00);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK(std::get<PasswordSecurity>(std::get<ConnectRequest>(decoded.value()).mechanism)
                  .password.empty());
    }

    SECTION("密码只接受 0x20 至 0x7E 的可打印字符") {
        request.mechanism = PasswordSecurity{std::string("ab") + '\x01'};
        CHECK(encode_connection(ConnectionApdu{request}).error().code == ErrorCode::invalid_value);
        request.mechanism = PasswordSecurity{std::string("ab") + '\x7f'};
        CHECK(encode_connection(ConnectionApdu{request}).error().code == ErrorCode::invalid_value);
        // 边界字符本身合法。
        request.mechanism = PasswordSecurity{std::string("\x20\x7e")};
        CHECK(static_cast<bool>(encode_connection(ConnectionApdu{request})));
    }

    SECTION("对称加密与签名认证携带密文与签名两段") {
        request.mechanism = SymmetrySecurity{hex("A1 B2"), hex("C3 D4")};
        auto symmetry = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(symmetry));
        const Bytes symmetry_bytes = symmetry.value();
        CHECK(symmetry_bytes[39] == 0x02);
        auto symmetry_back = decode_connection(ByteView{symmetry_bytes});
        REQUIRE(static_cast<bool>(symmetry_back));
        const auto& decoded_mechanism = std::get<ConnectRequest>(symmetry_back.value()).mechanism;
        REQUIRE(decoded_mechanism.index() == 2);
        const auto& security = std::get<SymmetrySecurity>(decoded_mechanism);
        const Bytes ciphertext = security.ciphertext;
        const Bytes signature = security.signature;
        CHECK(to_hex(ByteView{ciphertext}) == "A1 B2");
        CHECK(to_hex(ByteView{signature}) == "C3 D4");

        // 选择子 3 的线格式与 2 相同，但语义是签名认证。
        request.mechanism = SignatureSecurity{hex("01 02"), hex("03 04")};
        auto signed_bytes = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(signed_bytes));
        const Bytes signature_bytes = signed_bytes.value();
        CHECK(signature_bytes[39] == 0x03);
        auto signature_back = decode_connection(ByteView{signature_bytes});
        REQUIRE(static_cast<bool>(signature_back));
        const auto& signature_mechanism =
            std::get<ConnectRequest>(signature_back.value()).mechanism;
        REQUIRE(signature_mechanism.index() == 3);
        const auto& signed_security = std::get<SignatureSecurity>(signature_mechanism);
        const Bytes signed_ciphertext = signed_security.ciphertext;
        const Bytes signed_signature = signed_security.signature;
        CHECK(to_hex(ByteView{signed_ciphertext}) == "01 02");
        CHECK(to_hex(ByteView{signed_signature}) == "03 04");
    }

    SECTION("密文与签名都可以为空") {
        request.mechanism = SymmetrySecurity{{}, {}};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 与空认证相比只多出两段零长度，各占一个长度字节。
        const Bytes expected = fixture("connect-request.hex");
        CHECK(bytes.size() == expected.size() + 2);
    }

    SECTION("认证选择子只能是 0 至 3") {
        const Bytes prefix = fixture("connect-request.hex");
        Bytes bytes = prefix;
        bytes.resize(39);
        bytes.push_back(0x00);
        bytes.push_back(0x00);
        CHECK(static_cast<bool>(decode_connection(ByteView{bytes})));
        for (std::uint8_t choice : {4, 5, 0x80, 0xff}) {
            INFO("选择子=" << +choice);
            Bytes invalid = prefix;
            invalid.resize(39);
            invalid.push_back(choice);
            invalid.push_back(0x00);
            auto decoded = decode_connection(ByteView{invalid});
            REQUIRE_FALSE(static_cast<bool>(decoded));
            CHECK(decoded.error().code == ErrorCode::invalid_value);
            CHECK(decoded.error().context == "authentication choice");
        }
    }

    SECTION("密码分支的字节被截断时报输入不足") {
        const Bytes prefix = fixture("connect-request.hex");
        Bytes bytes = prefix;
        bytes.resize(39);
        bytes.push_back(0x01);
        bytes.push_back(0x06);
        bytes.push_back('1');
        bytes.push_back('2');
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::need_more_data);
        CHECK(decoded.error().context == "authentication bytes");
    }
}

TEST_CASE("CONNECT 响应按附录 D.2 向量往返", "[apdu][connection][connect]") {
    ConnectResponse response;
    response.piid_acd = 0;
    // 厂商 TOPS，软件/硬件 0102、日期 160731，八字节零扩展。
    response.factory.manufacturer = {'T', 'O', 'P', 'S'};
    response.factory.software_version = {'0', '1', '0', '2'};
    response.factory.software_date = {'1', '6', '0', '7', '3', '1'};
    response.factory.hardware_version = {'0', '1', '0', '2'};
    response.factory.hardware_date = {'1', '6', '0', '7', '3', '1'};
    response.parameters = all_capability_parameters();
    response.result = 0;

    expect_vector("connect-response.hex", ConnectionApdu{response});

    SECTION("厂商字段与参数逐项保持") {
        const Bytes bytes = fixture("connect-response.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ConnectResponse>(decoded.value());
        // 厂商字段是固定长度字节数组，逐字节核对。
        const FactoryVersion expected{{'T', 'O', 'P', 'S'},           {'0', '1', '0', '2'},
                                      {'1', '6', '0', '7', '3', '1'}, {'0', '1', '0', '2'},
                                      {'1', '6', '0', '7', '3', '1'}, {0, 0, 0, 0, 0, 0, 0, 0}};
        CHECK(back.factory.manufacturer == expected.manufacturer);
        CHECK(back.factory.software_version == expected.software_version);
        CHECK(back.factory.software_date == expected.software_date);
        CHECK(back.factory.hardware_version == expected.hardware_version);
        CHECK(back.factory.hardware_date == expected.hardware_date);
        CHECK(back.factory.extension == expected.extension);
        CHECK(back.parameters.version == 0x0010);
        CHECK(back.parameters.timeout_seconds == 100);
        CHECK_FALSE(back.security.has_value());
        CHECK_FALSE(back.time_tag.has_value());
        // 服务器 APDU 固定带 FollowReport 不存在标记。
        CHECK(bytes[73] == 0x00);
        CHECK(bytes[74] == 0x00);
    }

    SECTION("外层成功不等于业务成功") {
        // 结果非零表示被拒绝或需要认证，解码本身仍是成功状态。
        for (std::uint8_t result : {1, 2, 3, 4, 5, 255}) {
            INFO("CONNECT 结果=" << +result);
            ConnectResponse rejected = response;
            rejected.result = result;
            auto encoded = encode_connection(ConnectionApdu{rejected});
            REQUIRE(static_cast<bool>(encoded));
            const Bytes bytes = encoded.value();
            auto decoded = decode_connection(ByteView{bytes});
            REQUIRE(static_cast<bool>(decoded));
            const auto& back = std::get<ConnectResponse>(decoded.value());
            // 远端结果码原样保留，调用方必须自行判断是否建立关联。
            CHECK(back.result == result);
        }
    }

    SECTION("结果取值 6 至 254 未定义并被拒绝") {
        ConnectResponse invalid = response;
        invalid.result = 6;
        CHECK(encode_connection(ConnectionApdu{invalid}).error().code == ErrorCode::invalid_value);
        Bytes bytes = fixture("connect-response.hex");
        bytes[71] = 6;
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "CONNECT result");
    }

    SECTION("SecurityData 存在时携带随机数与签名") {
        const Bytes base = fixture("connect-response.hex");
        Bytes bytes = base;
        bytes[72] = 1;  // SecurityData 存在标记
        // 在 FollowReport 与 TimeTag 之前插入两段字节串。
        bytes.insert(bytes.begin() + 73, {0x02, 0xab, 0xcd, 0x01, 0xef});
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& security = std::get<ConnectResponse>(decoded.value()).security;
        REQUIRE(security.has_value());
        const Bytes random = security->random;
        const Bytes signature = security->signature;
        CHECK(to_hex(ByteView{random}) == "AB CD");
        CHECK(to_hex(ByteView{signature}) == "EF");

        auto encoded =
            encode_connection(ConnectionApdu{std::get<ConnectResponse>(decoded.value())});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes result = encoded.value();
        CHECK(to_hex(ByteView{result}) == to_hex(ByteView{bytes}));
    }

    SECTION("SecurityData 存在标记只能是 0 或 1") {
        Bytes bytes = fixture("connect-response.hex");
        bytes[72] = 2;
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "SecurityData presence");
    }

    SECTION("SecurityData 截断时报输入不足") {
        Bytes bytes = fixture("connect-response.hex");
        bytes[72] = 1;
        // 只保留随机数一段，并去掉签名长度字节。
        bytes[73] = 0x02;
        bytes[74] = 0xab;
        bytes[75] = 0xcd;
        bytes.resize(76);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::need_more_data);
        CHECK(decoded.error().context == "A-XDR length");
    }

    SECTION("带SecurityData 的响应逐字节截断全部拒绝") {
        Bytes full = fixture("connect-response.hex");
        full[72] = 1;
        full.insert(full.begin() + 73, {0x02, 0xab, 0xcd, 0x01, 0xef});
        REQUIRE(static_cast<bool>(decode_connection(ByteView{full})));
        for (std::size_t n = 0; n < full.size(); ++n) {
            INFO("截断到 " << n << " 字节");
            CHECK_FALSE(
                static_cast<bool>(decode_connection(ByteView{full}.subview(0, n), Limits{})));
        }
    }

    SECTION("不完整 FollowReport 选择符被拒绝") {
        Bytes bytes = fixture("connect-response.hex");
        bytes[73] = 1;
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::invalid_value);
        CHECK(decoded.error().context == "FollowReport choice");
    }

    SECTION("时间标签可附加在服务器响应尾部") {
        ConnectResponse tagged = response;
        tagged.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {1, 60}};
        auto encoded = encode_connection(ConnectionApdu{tagged});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        const Bytes base = fixture("connect-response.hex");
        CHECK(bytes.size() == base.size() + 10);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& tag = std::get<ConnectResponse>(decoded.value()).time_tag;
        REQUIRE(tag.has_value());
        CHECK(tag->allowed_delay.unit == 1);
        CHECK(tag->allowed_delay.interval == 60);
    }
}

TEST_CASE("RELEASE 家族按规范向量往返", "[apdu][connection][release]") {
    SECTION("释放请求无 TimeTag") {
        ReleaseRequest request;
        request.piid = 1;
        expect_vector("release-request.hex", ConnectionApdu{request});

        const Bytes bytes = fixture("release-request.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ReleaseRequest>(decoded.value());
        CHECK(back.piid == 1);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("释放响应只定义成功结果") {
        ReleaseResponse response;
        response.piid_acd = 1;
        response.result = 0;
        expect_vector("release-response.hex", ConnectionApdu{response});

        // 非零结果未定义，编解码两侧都拒绝。
        response.result = 1;
        CHECK(encode_connection(ConnectionApdu{response}).error().code == ErrorCode::invalid_value);
        require_connection_error("83 01 01 00 00", ErrorCode::invalid_value, "RELEASE result");
    }

    SECTION("释放通知携带建立时间与当前时间") {
        ReleaseNotification notice;
        notice.piid_acd = 2;
        notice.established_at = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};
        notice.current_time = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x00}};
        expect_vector("release-notification.hex", ConnectionApdu{notice});

        const Bytes bytes = fixture("release-notification.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ReleaseNotification>(decoded.value());
        CHECK(back.piid_acd == 2);
        // 两个时间相差一分钟，通知不携带毫秒。
        CHECK(back.established_at.value[3] == 0x13);
        CHECK(back.established_at.value[4] == 0x08);
        CHECK(back.established_at.value[5] == 0x05);
        CHECK(back.current_time.value[5] == 0x06);
        CHECK(back.established_at.value[6] == 0x00);
    }

    SECTION("释放通知的时间字段按原始字节保留") {
        // 日历不校验合法性，毫秒位可携带任意协议允许值。
        ReleaseNotification notice;
        notice.piid_acd = 2;
        notice.established_at = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0xff}};
        notice.current_time = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x00}};
        auto encoded = encode_connection(ConnectionApdu{notice});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK(std::get<ReleaseNotification>(decoded.value()).established_at.value[6] == 0xff);
    }

    SECTION("释放请求与响应都可携带 TimeTag") {
        ReleaseRequest request;
        request.piid = 1;
        request.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 10}};
        auto encoded = encode_connection(ConnectionApdu{request});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK(std::get<ReleaseRequest>(decoded.value()).time_tag->allowed_delay.interval == 10);

        ReleaseResponse response;
        response.piid_acd = 1;
        response.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x01}}, {3, 5}};
        auto response_encoded = encode_connection(ConnectionApdu{response});
        REQUIRE(static_cast<bool>(response_encoded));
        const Bytes response_bytes = response_encoded.value();
        auto response_back = decode_connection(ByteView{response_bytes});
        REQUIRE(static_cast<bool>(response_back));
        const auto& tag = std::get<ReleaseResponse>(response_back.value()).time_tag;
        REQUIRE(tag.has_value());
        CHECK(tag->allowed_delay.unit == 3);
    }

    SECTION("释放通知逐字节往返") {
        // 服务端 PIID-ACD 的bit7 是 ACD 标志，bit6 目前不参与保留位校验。
        ReleaseNotification notice;
        notice.piid_acd = 0x41;
        notice.established_at = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}};
        notice.current_time = DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x06, 0x00}};
        auto encoded = encode_connection(ConnectionApdu{notice});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        // ACD 标志位原样保留，调用方据此区分是否由服务器主动发起。
        CHECK((std::get<ReleaseNotification>(decoded.value()).piid_acd & 0x40) != 0);
    }
}

TEST_CASE("ERROR-Response 携带远端码往返", "[apdu][connection][error]") {
    SECTION("服务器方向的异常响应按向量往返") {
        ErrorResponse response;
        response.server = true;
        response.piid = 3;
        response.type = 2;  // 服务不支持
        expect_vector("error-response.hex", ConnectionApdu{response});

        const Bytes bytes = fixture("error-response.hex");
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        const auto& back = std::get<ErrorResponse>(decoded.value());
        CHECK(back.server);
        CHECK(back.piid == 3);
        // 远端结果码原样保存，本地不替换成宿主错误码。
        CHECK(back.type == 2);
        CHECK_FALSE(back.time_tag.has_value());
    }

    SECTION("客户端方向没有 FollowReport 标记") {
        ErrorResponse response;
        response.server = false;
        response.piid = 3;
        response.type = 2;
        auto encoded = encode_connection(ConnectionApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        // 服务标签 6E 加PIID、类型与时间标签标记共四字节。
        CHECK(bytes.size() == 4);
        CHECK(bytes[0] == 0x6e);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK_FALSE(std::get<ErrorResponse>(decoded.value()).server);

        // 客户端方向不应再出现 FollowReport 与时间标签两个字节。
        const Bytes with_follow = hex("6E 03 02 00 00 00");
        auto rejected = decode_connection(ByteView{with_follow});
        REQUIRE_FALSE(static_cast<bool>(rejected));
        CHECK(rejected.error().code == ErrorCode::trailing_data);
    }

    SECTION("异常类型只接受 1、2 与 255") {
        for (std::uint8_t type : {1, 2, 255}) {
            INFO("类型=" << +type);
            ErrorResponse response;
            response.server = true;
            response.piid = 0;
            response.type = type;
            CHECK(static_cast<bool>(encode_connection(ConnectionApdu{response})));
        }
        ErrorResponse invalid;
        invalid.server = true;
        invalid.piid = 0;
        for (std::uint8_t type : {0, 3, 4, 128, 254}) {
            INFO("类型=" << +type);
            invalid.type = type;
            CHECK(encode_connection(ConnectionApdu{invalid}).error().code ==
                  ErrorCode::invalid_value);
        }
        require_connection_error("EE 03 03 00 00", ErrorCode::invalid_value, "ERROR type");
        require_connection_error("EE 03 00 00 00", ErrorCode::invalid_value, "ERROR type");
    }

    SECTION("异常响应可携带 TimeTag") {
        ErrorResponse response;
        response.server = true;
        response.piid = 3;
        response.type = 1;  // 无法解析
        response.time_tag = TimeTag{DateTimeS{{0x07, 0xe0, 0x05, 0x13, 0x08, 0x05, 0x00}}, {0, 30}};
        auto encoded = encode_connection(ConnectionApdu{response});
        REQUIRE(static_cast<bool>(encoded));
        const Bytes bytes = encoded.value();
        const Bytes base = fixture("error-response.hex");
        CHECK(bytes.size() == base.size() + 10);
        auto decoded = decode_connection(ByteView{bytes});
        REQUIRE(static_cast<bool>(decoded));
        CHECK(std::get<ErrorResponse>(decoded.value()).time_tag->allowed_delay.interval == 30);
    }

    SECTION("PIID 保留位在两个方向都被校验") {
        require_connection_error("EE 40 02 00 00", ErrorCode::invalid_value, "PIID reserved bit");
        require_connection_error("6E 40 02 00", ErrorCode::invalid_value, "PIID reserved bit");
    }
}

TEST_CASE("连接类 APDU 的错误分类与资源上限", "[apdu][connection]") {
    SECTION("未分配的服务标签报不支持") {
        for (const char* text : {"00 00", "04 00", "08 00", "09 00", "FF 00", "80 00", "F0 00"}) {
            INFO("输入=" << text);
            require_connection_error(text, ErrorCode::unsupported_service, "connection service");
        }
    }

    SECTION("完整输入之后的多余字节报尾随数据") {
        require_connection_error("03 01 00 00", ErrorCode::trailing_data, "trailing bytes");
        require_connection_error("EE 03 02 00 00 00", ErrorCode::trailing_data, "trailing bytes");
        require_connection_error("01 00 00 00 B4 07 E0 05 13 04 08 05 00 00 A4 00",
                                 ErrorCode::trailing_data, "trailing bytes");
    }

    SECTION("逐字节截断全部拒绝") {
        const char* names[] = {"link-login-request.hex",   "link-login-response.hex",
                               "connect-request.hex",      "connect-response.hex",
                               "release-request.hex",      "release-response.hex",
                               "release-notification.hex", "error-response.hex"};
        for (const char* name : names) {
            INFO("向量=" << name);
            const Bytes bytes = fixture(name);
            for (std::size_t n = 0; n < bytes.size(); ++n) {
                INFO("截断到 " << n << " 字节");
                CHECK_FALSE(
                    static_cast<bool>(decode_connection(ByteView{bytes}.subview(0, n), Limits{})));
            }
            CHECK(static_cast<bool>(decode_connection(ByteView{bytes}, Limits{})));
        }
    }

    SECTION("输入字节上限") {
        const Bytes bytes = fixture("connect-request.hex");
        Limits limits;
        limits.max_data_bytes = 8;
        auto decoded = decode_connection(ByteView{bytes}, limits);
        REQUIRE_FALSE(static_cast<bool>(decoded));
        CHECK(decoded.error().code == ErrorCode::resource_limit);
        CHECK(decoded.error().context == "APDU input limit");
        // 恰好等于上限时放行。
        limits.max_data_bytes = bytes.size();
        CHECK(static_cast<bool>(decode_connection(ByteView{bytes}, limits)));
    }

    SECTION("输出字节上限") {
        ConnectRequest request;
        request.piid = 0;
        request.parameters = all_capability_parameters();
        Limits limits;
        limits.max_data_bytes = 16;
        auto encoded = encode_connection(ConnectionApdu{request}, limits);
        REQUIRE_FALSE(static_cast<bool>(encoded));
        CHECK(encoded.error().code == ErrorCode::resource_limit);
        CHECK(encoded.error().context == "output limit");
    }

    SECTION("可变认证字段也受输入上限约束") {
        // 密码长度按 max_data_bytes 预算，超限时不得分配任意长度。
        Bytes bytes = fixture("connect-request.hex");
        bytes.resize(39);
        bytes.push_back(0x01);
        bytes.push_back(0x81);  // 129 字节密码
        bytes.resize(bytes.size() + 129, 'a');
        bytes.push_back(0x00);
        Limits limits;
        limits.max_data_bytes = 48;
        auto decoded = decode_connection(ByteView{bytes}, limits);
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    SECTION("固定长度字段截断时报输入不足并给出字段名") {
        // 服务与 PIID 之后缺关联参数。
        require_connection_error("02 00 00", ErrorCode::need_more_data, "protocol version");
        require_connection_error("02 00 00 10 00", ErrorCode::need_more_data, "fixed field");
        require_connection_error("01 00", ErrorCode::need_more_data, "LINK type");
        require_connection_error("EE 03", ErrorCode::need_more_data, "ERROR type");
    }
}