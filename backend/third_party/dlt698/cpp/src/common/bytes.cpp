#include <cctype>
#include <dlt698/common/bytes.hpp>

namespace dlt698 {
Result<Bytes> from_hex(std::string_view text) {
    Bytes bytes;
    // high 保存尚未配对的高半字节；只允许在完整字节之间跳过空白。
    int high = -1;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) {
            if (high >= 0) return Error{ErrorCode::invalid_value, i, "space inside hex byte"};
            continue;
        }
        int digit = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
        if (digit < 0) return Error{ErrorCode::invalid_value, i, "hex digit"};
        if (high < 0)
            high = digit;
        else {
            bytes.push_back(static_cast<std::uint8_t>((high << 4) | digit));
            high = -1;
        }
    }
    if (high >= 0) return Error{ErrorCode::invalid_length, text.size(), "odd hex digit count"};
    return bytes;
}

std::string to_hex(ByteView bytes) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) text.push_back(' ');
        text.push_back(digits[bytes[i] >> 4]);
        text.push_back(digits[bytes[i] & 15]);
    }
    return text;
}
}  // namespace dlt698
