/**
 * @file test_support.hpp
 * @brief Catch2 测试公用辅助：十六进制转换、协议向量读取与错误断言。
 */
#pragma once
#include <catch_amalgamated.hpp>
#include <dlt698/common/bytes.hpp>
#include <dlt698/common/result.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace dlt698::test {

/**
 * @brief 将十六进制文本转换为字节序列，解析失败时终止当前用例。
 * @param[in] text 大小写均可的十六进制字符，完整字节之间允许空白。
 * @return 拥有内存的字节序列。
 */
inline Bytes hex(std::string_view text) {
    auto result = from_hex(text);
    REQUIRE(static_cast<bool>(result));
    return std::move(result).value();
}

/**
 * @brief 读取 tests/vectors 下的协议十六进制向量。
 * @param[in] name 向量文件名，不含目录。
 * @return 去除空白后的字节序列。
 * @note 向量缺失属于构建配置错误，直接终止用例而不是跳过，避免用例静默失效。
 */
inline Bytes fixture(std::string_view name) {
    const std::filesystem::path path{std::string(DLT698_VECTOR_DIR) + "/" + std::string(name)};
    std::ifstream input(path, std::ios::binary);
    INFO("无法读取协议向量: " << path.string());
    REQUIRE(input.good());
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return hex(text);
}

/**
 * @brief 为字节序列创建只读视图。
 * @param[in] bytes 生命周期须覆盖视图使用过程的字节序列。
 * @return 借用 bytes 的视图。
 * @note ByteView 刻意禁止绑定临时容器，因此 hex() 的结果必须先存入具名变量，
 * 再作为视图传入被测接口，不能写成 decode_data(hex("..."))。
 */
inline ByteView view(const Bytes& bytes) { return ByteView{bytes}; }

/**
 * @brief 匹配 DecodeFailure 携带的错误码与上下文字段。
 * @param[in] code 预期的错误分类。
 * @param[in] context 预期的字段名称，空串表示不检查该字段。
 * @return 可用于 CHECK_THROWS_MATCHES 的匹配器。
 */
inline auto decode_error(ErrorCode code, std::string_view context = {}) {
    return Catch::Matchers::Predicate<DecodeFailure>(
        [code, context](const DecodeFailure& failure) {
            return failure.error.code == code &&
                   (context.empty() || failure.error.context == context);
        },
        "DecodeFailure 携带预期错误码与上下文");
}

/**
 * @brief 断言结果成功并在失败时打印错误码与上下文。
 * @param[in] result 待检查的结果容器。
 * @return 成功值的常量引用，生命周期仍受结果容器约束。
 */
template <class T>
const T& require_ok(const Result<T>& result) {
    INFO("错误码=" << static_cast<int>(result.error().code)
                   << " 上下文=" << result.error().context);
    REQUIRE(static_cast<bool>(result));
    return result.value();
}

/**
 * @brief 断言结果失败并比对错误分类。
 * @param[in] result 待检查的结果容器。
 * @param[in] code 预期的错误分类。
 */
template <class T>
void require_error(const Result<T>& result, ErrorCode code) {
    INFO("实际错误码=" << static_cast<int>(result.error().code)
                       << " 上下文=" << result.error().context);
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
}

/**
 * @brief 校验完整输入接口对每个截断位置都拒绝，同时完整输入可解码。
 * @param[in] decode 接受 ByteView 并返回可判定成功与否的解码函数。
 * @param[in] bytes 完整编码结果。
 * @note 半个协议字段必须报 need_more_data，而不是被当作损坏数据静默丢弃。
 */
template <class Decode>
void require_truncation_rejected(Decode decode, ByteView bytes) {
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        INFO("截断到 " << n << " 字节");
        REQUIRE_FALSE(static_cast<bool>(decode(ByteView(bytes).subview(0, n))));
    }
    REQUIRE(static_cast<bool>(decode(bytes)));
}

/**
 * @brief 断言解码十六进制输入失败，并比对错误分类。
 * @param[in] text 十六进制输入文本。
 * @param[in] code 预期的错误分类。
 * @param[in] decode 解码函数，签名为 Result<T>(ByteView, const Limits&)。
 * @note ByteView 禁止绑定临时容器，因此先解析成具名字节再建立视图。
 */
template <class Decode>
void require_decode_error(const char* text, ErrorCode code, Decode decode,
                          const Limits& limits = {}) {
    INFO("输入=" << text);
    const Bytes bytes = hex(text);
    const auto result = decode(ByteView{bytes}, limits);
    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == code);
}

}  // namespace dlt698::test

/**
 * @brief 断言表达式抛出 DecodeFailure，且错误码与上下文符合预期。
 * @param[in] expr 期望抛出 DecodeFailure 的表达式。
 * @param[in] code 预期的错误分类。
 * @param[in] ... 可选的上下文字段名称，默认不检查。
 * @note 封装成宏是因为 decode_error 调用含逗号，直接展开会被 Catch2 断言宏拆成多个参数。
 */
#define CHECK_DECODE_ERROR(expr, code, ...)             \
    CHECK_THROWS_MATCHES(expr, ::dlt698::DecodeFailure, \
                         ::dlt698::test::decode_error((code), ##__VA_ARGS__))
