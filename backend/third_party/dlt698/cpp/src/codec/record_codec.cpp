#include <dlt698/codec/record_codec.hpp>
#include <functional>

#include "record_detail.hpp"

namespace dlt698::codec {
namespace {
using namespace model;
using ReadValue = std::function<Data(Reader&)>;
using WriteValue = std::function<void(Writer&, const Data&)>;

void invalid(std::size_t at, const char* field) {
    throw DecodeFailure({ErrorCode::invalid_value, at, field});
}

void count(Writer& w, std::size_t n, const Limits& l) {
    if (n > l.max_elements)
        throw DecodeFailure({ErrorCode::resource_limit, w.size(), "selector count"});
    write_length(w, n);
}

std::size_t count(Reader& r, const Limits& l, std::size_t minimum) {
    const auto n = read_length(r, l.max_elements);
    // 所有序列先检查最小输入长度，再分配，防止伪造数量触发大分配。
    if (n > r.remaining() / minimum)
        throw DecodeFailure({ErrorCode::need_more_data, r.position(), "selector count"});
    return n;
}

DateTimeS date(Reader& r) {
    DateTimeS d;
    for (auto& b : d.value) b = r.u8("selector date");
    return d;
}

void date(Writer& w, const DateTimeS& d) { w.bytes({d.value.data(), d.value.size()}); }

Ti ti(Reader& r) {
    const auto u = r.u8("TI unit");
    if (u > 5) invalid(r.position() - 1, "TI unit");
    return {u, static_cast<std::uint16_t>(r.be(2, "TI interval"))};
}

void ti(Writer& w, Ti t) {
    if (t.unit > 5) invalid(w.size(), "TI unit");
    w.u8(t.unit);
    w.be(t.interval, 2);
}

Selector2 range(Reader& r, const ReadValue& read) {
    return {read_oad(r), read(r), read(r), read(r)};
}

void range(Writer& w, const Selector2& s, const WriteValue& write) {
    write_oad(w, s.attribute);
    write(w, s.begin);
    write(w, s.end);
    write(w, s.interval);
}

void region_type(const Region& v, std::size_t index, std::size_t at) {
    const auto t = index == 5 ? DataType::uint8 : index == 6 ? DataType::tsa : DataType::uint16;
    if (v.boundary > 3 || v.begin.type() != t || v.end.type() != t)
        invalid(at, "MS Region type/boundary");
}
}  // namespace

Ms read_ms_impl(Reader& r, const Limits& l, const ReadValue& read) {
    const auto branch = r.u8("MS choice");
    if (branch == 0) return NoMeters{};
    if (branch == 1) return AllMeters{};
    if (branch > 7) invalid(r.position() - 1, "MS choice");
    const auto n = count(r, l, branch == 4 ? 2 : 1);
    if (branch == 2) {
        MeterTypes v;
        for (std::size_t i = 0; i < n; ++i) v.values.push_back(r.u8("meter type"));
        return v;
    }
    if (branch == 3) {
        MeterAddresses v;
        for (std::size_t i = 0; i < n; ++i) {
            const auto b = r.bytes(read_length(r, 17), "TSA");
            if (b.empty() || (b[0] & 15) + 2u != b.size()) invalid(r.position(), "TSA length");
            v.values.push_back(Tsa{b});
        }
        return v;
    }
    if (branch == 4) {
        MeterNumbers v;
        for (std::size_t i = 0; i < n; ++i)
            v.values.push_back(static_cast<std::uint16_t>(r.be(2, "meter number")));
        return v;
    }
    std::vector<Region> regions;
    for (std::size_t i = 0; i < n; ++i) {
        Region v{r.u8("Region boundary"), read(r), read(r)};
        region_type(v, branch, r.position());
        regions.push_back(std::move(v));
    }
    if (branch == 5) return MeterTypeRegions{std::move(regions)};
    if (branch == 6) return MeterAddressRegions{std::move(regions)};
    return MeterNumberRegions{std::move(regions)};
}

void write_ms_impl(Writer& w, const Ms& value, const Limits& l, const WriteValue& write) {
    w.u8(static_cast<std::uint8_t>(value.index()));
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, NoMeters> && !std::is_same_v<T, AllMeters>) {
                count(w, v.values.size(), l);
                for (const auto& x : v.values) {
                    if constexpr (std::is_same_v<T, MeterTypes>)
                        w.u8(x);
                    else if constexpr (std::is_same_v<T, MeterNumbers>)
                        w.be(x, 2);
                    else if constexpr (std::is_same_v<T, MeterAddresses>) {
                        if (x.value.empty() || x.value.size() > 17 ||
                            (x.value[0] & 15) + 2u != x.value.size())
                            invalid(w.size(), "TSA length");
                        write_length(w, x.value.size());
                        w.bytes(x.value);
                    } else {
                        region_type(x, value.index(), w.size());
                        w.u8(x.boundary);
                        write(w, x.begin);
                        write(w, x.end);
                    }
                }
            }
        },
        value);
}

Rsd read_rsd_impl(Reader& r, const Limits& l, const ReadValue& read) {
    const auto b = r.u8("RSD choice");
    switch (b) {
        case 0:
            return SelectAll{};
        case 1:
            return Selector1{read_oad(r), read(r)};
        case 2:
            return range(r, read);
        case 3: {
            Selector3 v;
            const auto n = count(r, l, 7);
            for (std::size_t i = 0; i < n; ++i) v.ranges.push_back(range(r, read));
            return v;
        }
        case 4:
            return Selector4{date(r), read_ms_impl(r, l, read)};
        case 5:
            return Selector5{date(r), read_ms_impl(r, l, read)};
        case 6:
            return Selector6{date(r), date(r), ti(r), read_ms_impl(r, l, read)};
        case 7:
            return Selector7{date(r), date(r), ti(r), read_ms_impl(r, l, read)};
        case 8:
            return Selector8{date(r), date(r), ti(r), read_ms_impl(r, l, read)};
        case 9:
            return Selector9{r.u8("previous record")};
        case 10:
            return Selector10{r.u8("latest records"), read_ms_impl(r, l, read)};
        default:
            invalid(r.position() - 1, "RSD choice");
    }
    return SelectAll{};
}

void write_rsd_impl(Writer& w, const Rsd& value, const Limits& l, const WriteValue& write) {
    w.u8(static_cast<std::uint8_t>(value.index()));
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Selector1>) {
                write_oad(w, v.attribute);
                write(w, v.value);
            } else if constexpr (std::is_same_v<T, Selector2>)
                range(w, v, write);
            else if constexpr (std::is_same_v<T, Selector3>) {
                count(w, v.ranges.size(), l);
                for (const auto& s : v.ranges) range(w, s, write);
            } else if constexpr (std::is_same_v<T, Selector4> || std::is_same_v<T, Selector5>) {
                date(w, v.time);
                write_ms_impl(w, v.meters, l, write);
            } else if constexpr (std::is_same_v<T, Selector6> || std::is_same_v<T, Selector7> ||
                                 std::is_same_v<T, Selector8>) {
                date(w, v.begin);
                date(w, v.end);
                ti(w, v.interval);
                write_ms_impl(w, v.meters, l, write);
            } else if constexpr (std::is_same_v<T, Selector9>)
                w.u8(v.previous);
            else if constexpr (std::is_same_v<T, Selector10>) {
                w.u8(v.latest);
                write_ms_impl(w, v.meters, l, write);
            }
        },
        value);
}

Rcsd read_rcsd(Reader& r, const Limits& l) {
    Rcsd result;
    const auto n = count(r, l, 5);
    std::size_t total = n;
    for (std::size_t i = 0; i < n; ++i) {
        const auto branch = r.u8("CSD choice");
        const auto oad = read_oad(r);
        if (branch == 0)
            result.emplace_back(oad);
        else if (branch == 1) {
            const auto m = count(r, l, 4);
            if (m > l.max_elements - total)
                throw DecodeFailure(
                    {ErrorCode::resource_limit, r.position(), "RCSD total descriptors"});
            total += m;
            Road road{oad, {}};
            for (std::size_t j = 0; j < m; ++j) road.associated.push_back(read_oad(r));
            result.emplace_back(std::move(road));
        } else
            invalid(r.position() - 5, "CSD choice");
    }
    return result;
}

void write_rcsd(Writer& w, const Rcsd& value, const Limits& l) {
    count(w, value.size(), l);
    std::size_t total = value.size();
    for (const auto& c : value) {
        w.u8(static_cast<std::uint8_t>(c.index()));
        if (const auto oad = std::get_if<Oad>(&c))
            write_oad(w, *oad);
        else {
            const auto& road = std::get<Road>(c);
            write_oad(w, road.attribute);
            if (road.associated.size() > l.max_elements - total)
                throw DecodeFailure(
                    {ErrorCode::resource_limit, w.size(), "RCSD total descriptors"});
            total += road.associated.size();
            count(w, road.associated.size(), l);
            for (const auto& o : road.associated) write_oad(w, o);
        }
    }
}

Ms read_ms(Reader& r, const Limits& l) {
    return read_ms_impl(r, l, [&](Reader& in) { return read_data(in, l); });
}

void write_ms(Writer& w, const Ms& v, const Limits& l) {
    write_ms_impl(w, v, l, [&](Writer& out, const Data& d) { write_data(out, d, l); });
}

Rsd read_rsd(Reader& r, const Limits& l) {
    return read_rsd_impl(r, l, [&](Reader& in) { return read_data(in, l); });
}

void write_rsd(Writer& w, const Rsd& v, const Limits& l) {
    write_rsd_impl(w, v, l, [&](Writer& out, const Data& d) { write_data(out, d, l); });
}

namespace detail {
namespace {
Road road(Reader& r, const Limits& l) {
    Road v{read_oad(r), {}};
    const auto n = count(r, l, 4);
    for (std::size_t i = 0; i < n; ++i) v.associated.push_back(read_oad(r));
    return v;
}

void road(Writer& w, const Road& v, const Limits& l) {
    write_oad(w, v.attribute);
    count(w, v.associated.size(), l);
    for (const auto& a : v.associated) write_oad(w, a);
}

Csd csd(Reader& r, const Limits& l) {
    const auto c = r.u8("CSD choice");
    if (c == 0) return read_oad(r);
    if (c == 1) return road(r, l);
    invalid(r.position() - 1, "CSD choice");
    return Oad{};
}

void csd(Writer& w, const Csd& v, const Limits& l) {
    w.u8(static_cast<std::uint8_t>(v.index()));
    if (const auto a = std::get_if<Oad>(&v))
        write_oad(w, *a);
    else
        road(w, std::get<Road>(v), l);
}
}  // namespace

RecordData read_descriptor(Reader& r, DataType type, const Limits& l, std::size_t depth,
                           std::size_t& nodes) {
    const ReadValue read = [&](Reader& in) { return read_nested(in, l, depth + 1, nodes); };
    // 描述符中的 Data 继续使用外层树的预算，不能在 Region/RSD 递归处重新计数。
    switch (type) {
        case DataType::road:
            return RecordData{road(r, l)};
        case DataType::csd:
            return RecordData{csd(r, l)};
        case DataType::rcsd:
            return RecordData{read_rcsd(r, l)};
        case DataType::rsd:
            return RecordData{read_rsd_impl(r, l, read)};
        case DataType::ms:
            return RecordData{read_ms_impl(r, l, read)};
        case DataType::region: {
            const auto b = r.u8("Region boundary");
            if (b > 3) invalid(r.position() - 1, "Region boundary");
            return RecordData{Region{b, read(r), read(r)}};
        }
        default:
            invalid(r.position(), "record Data type");
    }
    return RecordData{Rcsd{}};
}

void write_descriptor(Writer& w, const RecordData& v, const Limits& l, std::size_t depth,
                      std::size_t& nodes) {
    const WriteValue write = [&](Writer& out, const Data& d) {
        write_nested(out, d, l, depth + 1, nodes);
    };
    switch (v.type()) {
        case DataType::road:
            road(w, v.as<Road>(), l);
            break;
        case DataType::csd:
            csd(w, v.as<Csd>(), l);
            break;
        case DataType::rcsd:
            write_rcsd(w, v.as<Rcsd>(), l);
            break;
        case DataType::rsd:
            write_rsd_impl(w, v.as<Rsd>(), l, write);
            break;
        case DataType::ms:
            write_ms_impl(w, v.as<Ms>(), l, write);
            break;
        case DataType::region: {
            const auto& region = v.as<Region>();
            if (region.boundary > 3) invalid(w.size(), "Region boundary");
            w.u8(region.boundary);
            write(w, region.begin);
            write(w, region.end);
            break;
        }
        default:
            invalid(w.size(), "moved record Data");
    }
}
}  // namespace detail
}  // namespace dlt698::codec

namespace dlt698::model {
DataType RecordData::type() const {
    if (!impl_) return DataType::null;
    constexpr DataType types[] = {DataType::road, DataType::region, DataType::rsd,
                                  DataType::csd,  DataType::ms,     DataType::rcsd};
    return types[impl_->payload.index()];
}

bool operator==(const RecordData& a, const RecordData& b) {
    if (a.impl_ == b.impl_) return true;
    return a.impl_ && b.impl_ && a.impl_->payload == b.impl_->payload;
}
}  // namespace dlt698::model
