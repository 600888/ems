#include <algorithm>
#include <cstring>
#include <dlt698/codec/data_codec.hpp>
#include <limits>
#include <type_traits>

#include "record_detail.hpp"
#include "security_detail.hpp"

namespace dlt698::codec {
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "Protocol requires binary32 and binary64");

namespace {
[[noreturn]] void invalid(std::size_t offset, const char* field) {
    throw DecodeFailure({ErrorCode::invalid_value, offset, field});
}

bool utf8_valid(const Bytes& bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        auto c = bytes[i++];
        if (c < 0x80) continue;
        std::size_t n;
        std::uint32_t cp, minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            cp = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            cp = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            cp = c & 7;
            minimum = 0x10000;
        } else
            return false;
        if (n > bytes.size() - i) return false;
        while (n--) {
            c = bytes[i++];
            if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 63);
        }
        // 拒绝过长编码、代理项和超出 Unicode 范围的码点，避免接受非规范 UTF-8。
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

void validate_text(const Bytes& bytes, model::DataType tag, std::size_t offset) {
    if (tag == model::DataType::visible_string &&
        std::any_of(bytes.begin(), bytes.end(), [](auto c) { return c < 0x20 || c > 0x7e; }))
        invalid(offset, "visible-string character");
    if (tag == model::DataType::utf8_string && !utf8_valid(bytes)) invalid(offset, "UTF8-string");
}

void validate_bits(const model::BitString& bits, std::size_t offset) {
    if (bits.value.size() != bits.bit_count / 8 + (bits.bit_count % 8 != 0))
        invalid(offset, "bit-string length");
    // 位串从高位开始存放，末字节中未使用的低位必须全部为零。
    if (bits.bit_count % 8 && (bits.value.back() & ((1u << (8 - bits.bit_count % 8)) - 1)))
        invalid(offset, "bit-string padding");
}

template <class T>
T signed_value(std::uint64_t bits) {
    constexpr auto width = sizeof(T) * 8;
    const auto mask = std::numeric_limits<std::make_unsigned_t<T>>::max();
    if (!(bits & (std::uint64_t{1} << (width - 1)))) return static_cast<T>(bits);
    // 通过补码的反码计算负值，避免把超出范围的无符号数直接转换为有符号数。
    // -1 - 反码值也能安全表示 INT64_MIN，不会先对最小负数取反。
    return static_cast<T>(-1 - static_cast<std::int64_t>((~bits) & mask));
}

template <class T>
T number(Reader& r) {
    const auto bits = r.be(sizeof(T), "numeric value");
    if constexpr (std::is_floating_point_v<T>) {
        using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        static_assert(std::numeric_limits<T>::is_iec559, "IEEE 754 is required");
        const auto raw = static_cast<U>(bits);
        T value;
        // 先将大端字节组合为整数位模式，再用 memcpy 保留浮点原始位（包括 NaN）。
        std::memcpy(&value, &raw, sizeof(T));
        return value;
    } else if constexpr (std::is_signed_v<T>)
        return signed_value<T>(bits);
    else
        return static_cast<T>(bits);
}

template <class T>
void write_number(Writer& w, T value) {
    if constexpr (std::is_floating_point_v<T>) {
        using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        U bits;
        std::memcpy(&bits, &value, sizeof(T));
        w.be(bits, sizeof(T));
    } else
        w.be(static_cast<std::uint64_t>(value), sizeof(T));
}

template <class T>
T calendar(Reader& r) {
    T result;
    // 原样保存日历字节，使 FF/FFFF 未指定值不会被本机日期或时区规则改写。
    for (auto& b : result.value) b = r.u8("calendar field");
    return result;
}

void bounded_nodes(std::size_t& nodes, std::size_t depth, const Limits& limits,
                   std::size_t offset) {
    // nodes 在整个 Data 树内共享，既计算容器，也计算叶节点，不能逐层重置预算。
    if (depth > limits.max_depth || nodes >= limits.max_elements)
        throw DecodeFailure({ErrorCode::resource_limit, offset, "Data depth/node limit"});
    ++nodes;
}

model::Data read_impl(Reader& r, const Limits& limits, std::size_t depth, std::size_t& nodes) {
    bounded_nodes(nodes, depth, limits, r.position());
    const auto offset = r.position();
    const auto tag = static_cast<model::DataType>(r.u8("Data tag"));
    using namespace model;
    switch (tag) {
        case DataType::null:
            return Null{};
        case DataType::boolean: {
            const auto b = r.u8("boolean");
            if (b > 1) invalid(offset, "boolean");
            return Boolean{b != 0};
        }
        case DataType::int8:
            return Int8{number<std::int8_t>(r)};
        case DataType::int16:
            return Int16{number<std::int16_t>(r)};
        case DataType::int32:
            return Int32{number<std::int32_t>(r)};
        case DataType::int64:
            return Int64{number<std::int64_t>(r)};
        case DataType::uint8:
            return UInt8{number<std::uint8_t>(r)};
        case DataType::uint16:
            return UInt16{number<std::uint16_t>(r)};
        case DataType::uint32:
            return UInt32{number<std::uint32_t>(r)};
        case DataType::uint64:
            return UInt64{number<std::uint64_t>(r)};
        case DataType::enumeration:
            return Enum{r.u8("enum")};
        case DataType::float32:
            return Float32{number<float>(r)};
        case DataType::float64:
            return Float64{number<double>(r)};
        case DataType::array:
        case DataType::structure: {
            const auto count = read_length(r, limits.max_elements);
            if (count > limits.max_elements - nodes)
                throw DecodeFailure(
                    {ErrorCode::resource_limit, offset, "Data aggregate node limit"});
            // 每个元素至少占一个类型标签，分配容器前先排除明显不完整的输入。
            r.require(count, "container element tags");
            std::vector<Data> values;
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                values.push_back(read_impl(r, limits, depth + 1, nodes));
            if (tag == DataType::array) return Array{std::move(values)};
            return Structure{std::move(values)};
        }
        case DataType::bit_string: {
            // 长度字段表示位数；从字节上限换算时先检查乘法溢出，再按位数向上取整读取。
            const auto max_bits =
                limits.max_data_bytes > std::numeric_limits<std::size_t>::max() / 8
                    ? std::numeric_limits<std::size_t>::max()
                    : limits.max_data_bytes * 8;
            const auto n = read_length(r, max_bits);
            BitString result{n, r.bytes(n / 8 + (n % 8 != 0), "bit-string")};
            validate_bits(result, offset);
            return result;
        }
        case DataType::octet_string:
        case DataType::visible_string:
        case DataType::utf8_string:
        case DataType::tsa: {
            const auto count = read_length(r, tag == DataType::tsa
                                                  ? std::min<std::size_t>(17, limits.max_data_bytes)
                                                  : limits.max_data_bytes);
            auto bytes = r.bytes(count, "string");
            if (tag == DataType::octet_string) return OctetString{std::move(bytes)};
            if (tag == DataType::tsa) {
                // TSA 内容为地址描述字节加地址；低四位保存地址字节数减一。
                if (bytes.empty() || (bytes[0] & 15) + 2u != bytes.size())
                    invalid(offset, "TSA address length");
                return Tsa{std::move(bytes)};
            }
            validate_text(bytes, tag, offset);
            std::string text(bytes.begin(), bytes.end());
            if (tag == DataType::visible_string) return VisibleString{std::move(text)};
            return Utf8String{std::move(text)};
        }
        case DataType::date_time:
            return calendar<DateTime>(r);
        case DataType::date:
            return calendar<Date>(r);
        case DataType::time:
            return calendar<Time>(r);
        case DataType::date_time_s:
            return calendar<DateTimeS>(r);
        case DataType::oi:
            return Oi{number<std::uint16_t>(r)};
        case DataType::oad:
            return read_oad(r);
        case DataType::omd:
            return Omd{number<std::uint16_t>(r), r.u8("method"), r.u8("mode")};
        case DataType::ti: {
            auto unit = r.u8("TI unit");
            if (unit > 5) invalid(offset, "TI unit");
            return Ti{unit, number<std::uint16_t>(r)};
        }
        case DataType::road:
        case DataType::region:
        case DataType::rsd:
        case DataType::csd:
        case DataType::ms:
        case DataType::rcsd:
            return detail::read_descriptor(r, tag, limits, depth, nodes);
        case DataType::mac:
            return Mac{fields::octets(r, limits)};
        case DataType::rn:
            return Rn{fields::octets(r, limits)};
        case DataType::sid:
            return fields::sid(r, limits);
        case DataType::sid_mac:
            return fields::sid_mac(r, limits);
        case DataType::comdcb:
            return fields::comdcb(r);
        case DataType::scaler_unit:
            return ScalerUnit{number<std::int8_t>(r), r.u8("physical unit")};
        default:
            throw DecodeFailure({ErrorCode::unsupported_tag, offset, "Data tag"});
    }
}

void write_impl(Writer& w, const model::Data& data, const Limits& limits, std::size_t depth,
                std::size_t& nodes) {
    bounded_nodes(nodes, depth, limits, w.size());
    const auto offset = w.size();
    // 由 variant 中的精确包装类型生成标签，保持 array/structure、Enum/UInt8 等类型差异。
    w.u8(static_cast<std::uint8_t>(data.type()));
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            using namespace model;
            if constexpr (std::is_same_v<T, Null>)
                return;
            else if constexpr (std::is_same_v<T, Array> || std::is_same_v<T, Structure>) {
                if (value.value.size() > limits.max_elements - nodes)
                    throw DecodeFailure(
                        {ErrorCode::resource_limit, offset, "Data aggregate node limit"});
                write_length(w, value.value.size());
                for (const auto& child : value.value)
                    write_impl(w, child, limits, depth + 1, nodes);
            } else if constexpr (std::is_same_v<T, BitString>) {
                validate_bits(value, offset);
                write_length(w, value.bit_count);
                w.bytes(value.value);
            } else if constexpr (std::is_same_v<T, RecordData>)
                detail::write_descriptor(w, value, limits, depth, nodes);
            else if constexpr (std::is_same_v<T, Sid>)
                fields::sid(w, value);
            else if constexpr (std::is_same_v<T, SidMac>)
                fields::sid_mac(w, value);
            else if constexpr (std::is_same_v<T, Comdcb>)
                fields::comdcb(w, value);
            else if constexpr (std::is_same_v<T, Mac> || std::is_same_v<T, Rn>)
                fields::octets(w, value.value);
            else if constexpr (std::is_same_v<T, Oad>)
                write_oad(w, value);
            else if constexpr (std::is_same_v<T, Omd>) {
                w.be(value.oi, 2);
                w.u8(value.method);
                w.u8(value.mode);
            } else if constexpr (std::is_same_v<T, Ti>) {
                if (value.unit > 5) invalid(offset, "TI unit");
                w.u8(value.unit);
                w.be(value.interval, 2);
            } else if constexpr (std::is_same_v<T, ScalerUnit>) {
                write_number(w, value.scaler);
                w.u8(value.unit);
            } else if constexpr (std::is_same_v<T, VisibleString> ||
                                 std::is_same_v<T, Utf8String>) {
                if (value.value.size() > limits.max_data_bytes)
                    throw DecodeFailure({ErrorCode::resource_limit, offset, "string limit"});
                const Bytes bytes(value.value.begin(), value.value.end());
                validate_text(bytes, T::type, offset);
                write_length(w, bytes.size());
                w.bytes(bytes);
            } else if constexpr (std::is_same_v<T, OctetString> || std::is_same_v<T, Tsa>) {
                if constexpr (std::is_same_v<T, Tsa>) {
                    if (value.value.empty() || value.value.size() > 17 ||
                        (value.value[0] & 15) + 2u != value.value.size())
                        invalid(offset, "TSA address length");
                }
                write_length(w, value.value.size());
                w.bytes(value.value);
            } else if constexpr (std::is_same_v<T, DateTime> || std::is_same_v<T, Date> ||
                                 std::is_same_v<T, Time> || std::is_same_v<T, DateTimeS>) {
                w.bytes({value.value.data(), value.value.size()});
            } else if constexpr (std::is_same_v<T, Boolean>)
                w.u8(value.value ? 1 : 0);
            else
                write_number(w, value.value);
        },
        data.payload);
}
}  // namespace

namespace detail {
model::Data read_nested(Reader& r, const Limits& l, std::size_t depth, std::size_t& nodes) {
    return read_impl(r, l, depth, nodes);
}

void write_nested(Writer& w, const model::Data& v, const Limits& l, std::size_t depth,
                  std::size_t& nodes) {
    write_impl(w, v, l, depth, nodes);
}
}  // namespace detail

std::size_t read_length(Reader& r, std::size_t limit) {
    const auto offset = r.position();
    const auto first = r.u8("A-XDR length");
    std::size_t length = first;
    if (first & 0x80) {
        // 最高位为 1 时，低七位是后续大端长度值的字节宽度；不接受不定长形式。
        const auto width = static_cast<unsigned>(first & 0x7f);
        if (!width || width > sizeof(std::size_t))
            throw DecodeFailure({ErrorCode::invalid_length, offset, "A-XDR length width"});
        r.require(width, "A-XDR length bytes");
        length = 0;
        for (unsigned i = 0; i < width; ++i) {
            const auto byte = r.u8("A-XDR length byte");
            // 长形式禁止前导零，且长度小于 128 时必须使用单字节短形式。
            if (!i && !byte)
                throw DecodeFailure({ErrorCode::invalid_length, offset, "non-minimal length"});
            length = (length << 8) | byte;
        }
        if (length < 128)
            throw DecodeFailure({ErrorCode::invalid_length, offset, "non-minimal length"});
    }
    if (length > limit)
        throw DecodeFailure({ErrorCode::resource_limit, offset, "A-XDR length limit"});
    return length;
}

void write_length(Writer& w, std::size_t length) {
    if (length < 128) {
        w.u8(static_cast<std::uint8_t>(length));
        return;
    }
    std::size_t width = 0;
    // 只输出表示 length 必需的字节，确保与 read_length 的最短形式检查一致。
    for (auto n = length; n; n >>= 8) ++width;
    w.u8(static_cast<std::uint8_t>(0x80 | width));
    w.be(length, width);
}

model::Oad read_oad(Reader& r) {
    auto oi = static_cast<std::uint16_t>(r.be(2, "OI"));
    auto attr = r.u8("OAD attribute");
    return {oi, attr, r.u8("OAD index")};
}

void write_oad(Writer& w, const model::Oad& v) {
    w.be(v.oi, 2);
    w.u8(v.attribute);
    w.u8(v.index);
}

model::Data read_data(Reader& r, const Limits& limits, std::size_t depth) {
    std::size_t nodes = 0;
    return read_impl(r, limits, depth, nodes);
}

void write_data(Writer& w, const model::Data& v, const Limits& limits, std::size_t depth) {
    std::size_t nodes = 0;
    write_impl(w, v, limits, depth, nodes);
}

Result<model::Data> decode_data(ByteView input, const Limits& limits) {
    if (input.size() > limits.max_data_bytes)
        return Error{ErrorCode::resource_limit, 0, "Data input limit"};
    try {
        Reader r(input);
        auto data = read_data(r, limits);
        // 完整输入入口不允许尾随数据；组合读取函数则保留后续字段供 APDU 使用。
        r.finish();
        return data;
    } catch (const DecodeFailure& e) {
        // 内部通过异常中断嵌套解析，公开入口统一返回含偏移和字段名的错误。
        return e.error;
    }
}

Result<Bytes> encode_data(const model::Data& data, const Limits& limits) {
    try {
        Writer w(limits.max_data_bytes);
        write_data(w, data, limits);
        return w.take();
    } catch (const DecodeFailure& e) {
        return e.error;
    }
}
}  // namespace dlt698::codec
