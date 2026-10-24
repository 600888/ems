#include "store_detail.h"

#include <array>
#include <charconv>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// BCrypt 声明依赖 Windows 基础类型，必须保持包含顺序。
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#else
#include <sys/random.h>

#include <cerrno>
#endif

namespace ems::rtdb::detail {
Error error(ErrorCode c, WriteEffect e, const char* m) { return {c, e, m}; }

bool utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        if (c < 128) continue;
        unsigned n = 0, value = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            value = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            value = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            value = c & 7;
            minimum = 0x10000;
        } else
            return false;
        if (n > s.size() - i) return false;
        while (n--) {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

bool valid_code(std::string_view s, bool device) {
    if (s.empty() || s.size() > 64) return false;
    auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(s.front())) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (!alnum(s[i]) && s[i] != '_' && s[i] != '-') return false;
        if (s[i] == '-' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') return false;
    }
    if (device)
        for (auto reserved :
             {"meta", "rt", "config", "stream", "command", "channel", "resource", "process"})
            if (s == reserved) return false;
    return true;
}

std::string hash_tag(const RecordKey& k) {
    return "{" + std::string(k.scope.kind == ScopeKind::Station ? "station:" : "") + k.scope.code +
           "}";
}

std::string physical_key(const RecordKey& k) {
    std::string s = (k.kind == RecordKind::Point ? "ems:rt:point:" : "ems:") + hash_tag(k);
    for (const auto& part : k.device_path) s += ':' + part;
    switch (k.kind) {
        case RecordKind::Point:
            return s + ':' + k.name;
        case RecordKind::AssetState:
            return s + ":rt:state";
        case RecordKind::ChannelState:
            return s + ":channel:" + k.name + ":rt:state";
        case RecordKind::CommandState:
            return s + ":command:" + k.name;
    }
    return {};
}

std::optional<RecordKey> asset_key(const Scope& scope, std::string_view physical) {
    RecordKey key{scope, RecordKind::AssetState, {}, {}};
    const auto prefix = "ems:" + hash_tag(key);
    constexpr std::string_view suffix = ":rt:state";
    if (physical.size() < prefix.size() + suffix.size() ||
        physical.substr(0, prefix.size()) != prefix ||
        physical.substr(physical.size() - suffix.size()) != suffix)
        return std::nullopt;
    auto middle = physical.substr(prefix.size(), physical.size() - prefix.size() - suffix.size());
    while (!middle.empty()) {
        if (middle.front() != ':') return std::nullopt;
        middle.remove_prefix(1);
        const auto end = middle.find(':');
        key.device_path.emplace_back(middle.substr(0, end));
        if (end == std::string_view::npos) break;
        middle.remove_prefix(end);
    }
    if (!validate_record_key(key).empty()) return std::nullopt;
    return key;
}

std::uint64_t hash(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::uint32_t checksum(std::string_view s) {
    std::uint32_t crc = ~0u;
    for (unsigned char c : s) {
        crc ^= c;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

Result<std::string> token() {
    unsigned char bytes[16];
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return error(ErrorCode::Internal, WriteEffect::NotApplied, "OS random source failed");
#else
    // Nonblocking entropy acquisition: failure never invents a reusable revision.
    auto n = getrandom(bytes, sizeof(bytes), GRND_NONBLOCK);
    if (n != sizeof(bytes))
        return error(ErrorCode::Internal, WriteEffect::NotApplied, "OS random source unavailable");
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (auto b : bytes) {
        s += hex[b >> 4];
        s += hex[b & 15];
    }
    return s;
}

void append_u32(std::string& s, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) s += static_cast<char>((v >> (i * 8)) & 255);
}

bool take_u32(std::string_view& s, std::uint32_t& v) {
    if (s.size() < 4) return false;
    v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= std::uint32_t(static_cast<unsigned char>(s[i])) << (8 * i);
    s.remove_prefix(4);
    return true;
}

void append_string(std::string& s, std::string_view v) {
    append_u32(s, static_cast<std::uint32_t>(v.size()));
    s.append(v);
}

bool take_string(std::string_view& s, std::string& out) {
    std::uint32_t size;
    if (!take_u32(s, size) || size > s.size()) return false;
    out.assign(s.data(), size);
    s.remove_prefix(size);
    return true;
}

Result<std::string> encode_fields(const Fields& fields, std::size_t limit, WriteEffect effect) {
    if (fields.size() > max_fields)
        return error(ErrorCode::InvalidArgument, effect, "field count limit exceeded");
    std::size_t size = 4;
    for (const auto& f : fields) {
        if (f.first.empty() || f.first.size() > 128 || f.first.compare(0, 7, "_store_") == 0 ||
            !utf8(f.first) || !utf8(f.second))
            return error(ErrorCode::InvalidArgument, effect, "invalid or reserved UTF-8 field");
        if (f.second.size() > limit || size > limit ||
            8 + f.first.size() + f.second.size() > limit - size)
            return error(ErrorCode::ResourceExhausted, effect, "record byte limit exceeded");
        size += 8 + f.first.size() + f.second.size();
    }
    if (size > limit)
        return error(ErrorCode::ResourceExhausted, effect, "record byte limit exceeded");
    std::string out;
    out.reserve(size);
    append_u32(out, static_cast<std::uint32_t>(fields.size()));
    for (const auto& f : fields) {
        append_string(out, f.first);
        append_string(out, f.second);
    }
    return out;
}

Result<Fields> decode_fields(std::string_view data, std::size_t limit, WriteEffect effect) {
    std::uint32_t n;
    if (data.size() > limit || !take_u32(data, n) || n > max_fields)
        return error(ErrorCode::CorruptData, effect, "invalid record encoding");
    Fields fields;
    while (n--) {
        std::string k, v;
        if (!take_string(data, k) || !take_string(data, v) || !fields.emplace(k, v).second)
            return error(ErrorCode::CorruptData, effect, "invalid field encoding");
    }
    if (!data.empty() || !core::is_ok(encode_fields(fields, limit, effect)))
        return error(ErrorCode::CorruptData, effect, "invalid stored fields");
    return fields;
}

bool parse_size(const std::string& s, std::size_t& out) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

bool known_options(const BackendSpec& s, std::initializer_list<const char*> keys) {
    for (const auto& entry : s.options) {
        bool found = false;
        for (auto k : keys)
            if (entry.first == k) found = true;
        if (!found) return false;
    }
    return true;
}
}  // namespace ems::rtdb::detail
