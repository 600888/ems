/**
 * @file result.hpp
 * @brief 编解码和传输操作的错误信息与结果容器。
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace dlt698 {
/// 协议解析、资源管理及传输操作的错误分类。
enum class ErrorCode {
    need_more_data,
    invalid_length,
    invalid_value,
    unsupported_tag,
    unsupported_service,
    checksum_header,
    checksum_frame,
    resource_limit,
    trailing_data,
    closed,
    io_error,
    timeout,
    cancelled,
    busy,
    address_mismatch,
    direction_mismatch,
    not_associated,
    association_failed,
    remote_error
};

/// 失败诊断；offset 的单位由接口约定，二进制编解码通常使用字节偏移。
struct Error {
    ErrorCode code;                                ///< 错误分类。
    std::size_t offset = 0;                        ///< 输入或输出的出错位置；无对应位置时通常为零。
    std::string context;                           ///< 字段名称或传输层错误描述。
    std::optional<std::uint8_t> remote_code = {};  ///< 远端 ERROR-Response 等结果的原始编码。
};

/**
 * @brief 保存成功值或错误信息，访问前应先检查成功状态。
 * @tparam T 成功结果的类型。
 */
template <class T>
class [[nodiscard]] Result {
   public:
    /**
     * @brief 创建成功结果。
     * @param[in] value 移入容器的成功值。
     */
    Result(T value) : state_(std::move(value)) {}

    /**
     * @brief 创建失败结果。
     * @param[in] error 移入容器的错误信息。
     */
    Result(Error error) : state_(std::move(error)) {}

    /**
     * @brief 检查操作是否成功。
     * @return 保存成功值时返回 true。
     */
    explicit operator bool() const noexcept { return std::holds_alternative<T>(state_); }

    /**
     * @brief 访问可修改的成功值。
     * @return 容器内成功值的引用。
     * @throws std::bad_variant_access 当前结果为失败状态。
     */
    T& value() & { return std::get<T>(state_); }

    /**
     * @brief 访问只读成功值。
     * @return 容器内成功值的常量引用。
     * @throws std::bad_variant_access 当前结果为失败状态。
     */
    const T& value() const& { return std::get<T>(state_); }

    /**
     * @brief 从临时结果中获取可移出的成功值。
     * @return 成功值的右值引用，生命周期仍受结果容器约束。
     * @throws std::bad_variant_access 当前结果为失败状态。
     */
    T&& value() && { return std::get<T>(std::move(state_)); }

    /**
     * @brief 访问失败诊断。
     * @return 容器内错误信息的常量引用。
     * @throws std::bad_variant_access 当前结果为成功状态。
     */
    const Error& error() const { return std::get<Error>(state_); }

   private:
    std::variant<T, Error> state_;
};

/// 无成功返回值的操作结果，默认构造表示成功。
template <>
class [[nodiscard]] Result<void> {
   public:
    /** @brief 创建成功结果。 */
    Result() = default;

    /**
     * @brief 创建失败结果。
     * @param[in] error 移入容器的错误信息。
     */
    Result(Error error) : error_(std::move(error)) {}

    /**
     * @brief 检查操作是否成功。
     * @return 未保存错误信息时返回 true。
     */
    explicit operator bool() const noexcept { return !error_; }

    /**
     * @brief 访问失败诊断。
     * @return 容器内错误信息的常量引用。
     * @throws std::bad_optional_access 当前结果为成功状态。
     */
    const Error& error() const { return error_.value(); }

   private:
    std::optional<Error> error_;
};
}  // namespace dlt698
