/** @file record.hpp
 * @brief 记录行列选择器，保存所有标准 CHOICE 分支与精确 Data 类型。
 */
#pragma once
#include <dlt698/model/data.hpp>

namespace dlt698::model {
struct Road {
    Oad attribute;
    std::vector<Oad> associated;
};

using Csd = std::variant<Oad, Road>;
using Rcsd = std::vector<Csd>;

struct Region {
    std::uint8_t boundary = 0;
    Data begin;
    Data end;
};

struct NoMeters {};

struct AllMeters {};

struct MeterTypes {
    std::vector<std::uint8_t> values;
};

struct MeterAddresses {
    std::vector<Tsa> values;
};

struct MeterNumbers {
    std::vector<std::uint16_t> values;
};

struct MeterTypeRegions {
    std::vector<Region> values;
};

struct MeterAddressRegions {
    std::vector<Region> values;
};

struct MeterNumberRegions {
    std::vector<Region> values;
};

using Ms = std::variant<NoMeters, AllMeters, MeterTypes, MeterAddresses, MeterNumbers,
                        MeterTypeRegions, MeterAddressRegions, MeterNumberRegions>;

struct SelectAll {};

struct Selector1 {
    Oad attribute;
    Data value;
};

struct Selector2 {
    Oad attribute;
    Data begin;
    Data end;
    Data interval;
};

struct Selector3 {
    std::vector<Selector2> ranges;
};

struct Selector4 {
    DateTimeS time;
    Ms meters;
};

struct Selector5 {
    DateTimeS time;
    Ms meters;
};

struct Selector6 {
    DateTimeS begin;
    DateTimeS end;
    Ti interval;
    Ms meters;
};

struct Selector7 {
    DateTimeS begin;
    DateTimeS end;
    Ti interval;
    Ms meters;
};

struct Selector8 {
    DateTimeS begin;
    DateTimeS end;
    Ti interval;
    Ms meters;
};

struct Selector9 {
    std::uint8_t previous = 1;
};

struct Selector10 {
    std::uint8_t latest = 1;
    Ms meters;
};

using Rsd = std::variant<SelectAll, Selector1, Selector2, Selector3, Selector4, Selector5,
                         Selector6, Selector7, Selector8, Selector9, Selector10>;

/** @brief 比较列描述符，保留完整 OAD 与 ROAD 顺序。
 * @param[in] a 左侧列。
 * @param[in] b 右侧列。
 * @return 描述符一致时为 true。
 */
inline bool operator==(const Road& a, const Road& b) {
    return a.attribute == b.attribute && a.associated == b.associated;
}

/** @brief 比较 Region 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Region& a, const Region& b) {
    return a.boundary == b.boundary && a.begin == b.begin && a.end == b.end;
}

/** @brief 比较 NoMeters 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const NoMeters&, const NoMeters&) { return true; }

/** @brief 比较 AllMeters 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const AllMeters&, const AllMeters&) { return true; }

/** @brief 比较 MeterTypes 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterTypes& a, const MeterTypes& b) { return a.values == b.values; }

/** @brief 比较 MeterAddresses 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterAddresses& a, const MeterAddresses& b) {
    return a.values == b.values;
}

/** @brief 比较 MeterNumbers 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterNumbers& a, const MeterNumbers& b) {
    return a.values == b.values;
}

/** @brief 比较 MeterTypeRegions 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterTypeRegions& a, const MeterTypeRegions& b) {
    return a.values == b.values;
}

/** @brief 比较 MeterAddressRegions 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterAddressRegions& a, const MeterAddressRegions& b) {
    return a.values == b.values;
}

/** @brief 比较 MeterNumberRegions 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const MeterNumberRegions& a, const MeterNumberRegions& b) {
    return a.values == b.values;
}

/** @brief 比较 SelectAll 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const SelectAll&, const SelectAll&) { return true; }

/** @brief 比较 Selector1 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector1& a, const Selector1& b) {
    return a.attribute == b.attribute && a.value == b.value;
}

/** @brief 比较 Selector2 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector2& a, const Selector2& b) {
    return a.attribute == b.attribute && a.begin == b.begin && a.end == b.end &&
           a.interval == b.interval;
}

/** @brief 比较 Selector3 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector3& a, const Selector3& b) { return a.ranges == b.ranges; }

/** @brief 比较 Selector4 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector4& a, const Selector4& b) {
    return a.time == b.time && a.meters == b.meters;
}

/** @brief 比较 Selector5 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector5& a, const Selector5& b) {
    return a.time == b.time && a.meters == b.meters;
}

/** @brief 比较 Selector6 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector6& a, const Selector6& b) {
    return a.begin == b.begin && a.end == b.end && a.interval == b.interval && a.meters == b.meters;
}

/** @brief 比较 Selector7 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector7& a, const Selector7& b) {
    return a.begin == b.begin && a.end == b.end && a.interval == b.interval && a.meters == b.meters;
}

/** @brief 比较 Selector8 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector8& a, const Selector8& b) {
    return a.begin == b.begin && a.end == b.end && a.interval == b.interval && a.meters == b.meters;
}

/** @brief 比较 Selector9 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector9& a, const Selector9& b) { return a.previous == b.previous; }

/** @brief 比较 Selector10 的全部协议字段。
 * @param[in] a 左侧选择条件。
 * @param[in] b 右侧选择条件。
 * @return 字段一致时为 true。
 */
inline bool operator==(const Selector10& a, const Selector10& b) {
    return a.latest == b.latest && a.meters == b.meters;
}

struct RecordData::Impl {
    using Payload = std::variant<Road, Region, Rsd, Csd, Ms, Rcsd>;
    Payload payload;

    /** @brief 保存不可变描述符的有类型值。
     * @tparam T Payload 支持的描述符类型。
     * @param[in] value 被移动到节点的拥有型值。
     */
    template <class T>
    explicit Impl(T value) : payload(std::move(value)) {}
};

/** @brief 创建拥有内存且可安全共享的不可变记录描述符。
 * @tparam T 描述符类型。
 * @param[in] value 被移入节点的内容。
 */
template <class T>
RecordData::RecordData(T value) : impl_(std::make_shared<Impl>(std::move(value))) {}

/** @brief 按精确描述符类型读取内容。
 * @tparam T 保存的描述符类型。
 * @return 生命周期受节点约束的常量引用。
 * @throws std::bad_variant_access 类型不符或节点已经移动。
 */
template <class T>
const T& RecordData::as() const {
    if (!impl_) throw std::bad_variant_access();
    return std::get<T>(impl_->payload);
}
}  // namespace dlt698::model
