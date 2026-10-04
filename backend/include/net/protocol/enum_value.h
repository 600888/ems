#pragma once

#include <type_traits>

namespace ems::net {
/**
 * @brief 在配置、JSON 和第三方 API 边界取得枚举的原始数值。
 * @tparam Enum 强类型枚举。
 * @param[in] value 待转换的枚举值。
 * @return 枚举对应的底层整数，保持协议编码不变。
 */
template <typename Enum>
constexpr std::underlying_type_t<Enum> enum_value(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}
}  // namespace ems::net
