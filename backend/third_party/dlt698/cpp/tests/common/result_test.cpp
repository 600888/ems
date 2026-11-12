/**
 * @file result_test.cpp
 * @brief Result 容器与 Error 诊断的单元测试。
 */
#include <dlt698/common/result.hpp>
#include <string>
#include <utility>

#include "catch/test_support.hpp"

using namespace dlt698;

TEST_CASE("Result<T> 区分成功值与错误诊断", "[common][result]") {
    SECTION("成功态可读写值，错误接口抛出") {
        Result<int> ok{42};
        CHECK(static_cast<bool>(ok));
        CHECK(ok.value() == 42);
        ok.value() = 7;
        CHECK(ok.value() == 7);
        CHECK(ok);
        CHECK_THROWS_AS(ok.error(), std::bad_variant_access);
    }

    SECTION("临时结果可以移出成功值") {
        auto moved = std::move(Result<std::string>{std::string{"payload"}}).value();
        CHECK(moved == "payload");
    }

    SECTION("失败态保留错误码、偏移、上下文与远端码") {
        Result<int> failed{Error{ErrorCode::remote_error, 12, "GetResponse", 4}};
        CHECK_FALSE(static_cast<bool>(failed));
        CHECK_THROWS_AS(failed.value(), std::bad_variant_access);
        CHECK_THROWS_AS(std::move(failed).value(), std::bad_variant_access);
        const auto& error = failed.error();
        CHECK(error.code == ErrorCode::remote_error);
        CHECK(error.offset == 12);
        CHECK(error.context == "GetResponse");
        REQUIRE(error.remote_code.has_value());
        CHECK(*error.remote_code == 4);
    }

    SECTION("remote_code 缺省为空") {
        Result<int> failed{Error{ErrorCode::timeout, 0, "", std::nullopt}};
        CHECK_FALSE(failed.error().remote_code.has_value());
        CHECK(failed.error().offset == 0);
        CHECK(failed.error().context.empty());
    }

    SECTION("nodiscard 语义由调用方显式检查") {
        // 构造后不做任何判断时[[nodiscard]] 会产生告警，这里用 lambda 验证可读性。
        auto ignored = [&] { return Result<int>{1}; };
        CHECK(static_cast<bool>(ignored()));
    }
}

TEST_CASE("Result<void> 默认表示成功", "[common][result]") {
    SECTION("默认构造为成功") {
        Result<void> ok;
        CHECK(static_cast<bool>(ok));
        CHECK_THROWS_AS(ok.error(), std::bad_optional_access);
    }

    SECTION("携带错误后为失败") {
        Result<void> failed{Error{ErrorCode::io_error, 3, "write"}};
        CHECK_FALSE(static_cast<bool>(failed));
        CHECK(failed.error().code == ErrorCode::io_error);
        CHECK(failed.error().offset == 3);
        CHECK(failed.error().context == "write");
    }
}

TEST_CASE("ErrorCode 覆盖协议与传输各层失败原因", "[common][result]") {
    // 枚举值本身是协议语义的一部分，重排会破坏错误分支判断，故逐一固定。
    CHECK(static_cast<int>(ErrorCode::need_more_data) == 0);
    CHECK(static_cast<int>(ErrorCode::invalid_length) == 1);
    CHECK(static_cast<int>(ErrorCode::invalid_value) == 2);
    CHECK(static_cast<int>(ErrorCode::unsupported_tag) == 3);
    CHECK(static_cast<int>(ErrorCode::unsupported_service) == 4);
    CHECK(static_cast<int>(ErrorCode::checksum_header) == 5);
    CHECK(static_cast<int>(ErrorCode::checksum_frame) == 6);
    CHECK(static_cast<int>(ErrorCode::resource_limit) == 7);
    CHECK(static_cast<int>(ErrorCode::trailing_data) == 8);
    CHECK(static_cast<int>(ErrorCode::closed) == 9);
    CHECK(static_cast<int>(ErrorCode::io_error) == 10);
    CHECK(static_cast<int>(ErrorCode::timeout) == 11);
    CHECK(static_cast<int>(ErrorCode::cancelled) == 12);
    CHECK(static_cast<int>(ErrorCode::busy) == 13);
    CHECK(static_cast<int>(ErrorCode::address_mismatch) == 14);
    CHECK(static_cast<int>(ErrorCode::direction_mismatch) == 15);
    CHECK(static_cast<int>(ErrorCode::not_associated) == 16);
    CHECK(static_cast<int>(ErrorCode::association_failed) == 17);
    CHECK(static_cast<int>(ErrorCode::remote_error) == 18);
}