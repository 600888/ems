#pragma once
#include <realtime/backend_factory.h>

#include <cstdint>
#include <string_view>

namespace ems::realtime::detail {
constexpr std::size_t max_key_bytes = 2048;
constexpr std::size_t max_fields = 256;
Error error(ErrorCode, WriteEffect, const char*);
bool utf8(std::string_view);
bool valid_code(std::string_view, bool device = false);
std::string physical_key(const RecordKey&);
std::string hash_tag(const RecordKey&);
std::uint64_t hash(std::string_view);
std::uint32_t checksum(std::string_view);
Result<std::string> token();
Result<std::string> encode_fields(const Fields&, std::size_t limit, WriteEffect);
Result<Fields> decode_fields(std::string_view, std::size_t limit, WriteEffect);
void append_u32(std::string&, std::uint32_t);
bool take_u32(std::string_view&, std::uint32_t&);
void append_string(std::string&, std::string_view);
bool take_string(std::string_view&, std::string&);
bool parse_size(const std::string&, std::size_t&);
bool known_options(const BackendSpec&, std::initializer_list<const char*>);
}  // namespace ems::realtime::detail
