/**
 * @file data.hpp
 * @brief 保留协议类型标签的 Data 模型及对象描述符。
 */
#pragma once
#include <array>
#include <dlt698/common/bytes.hpp>
#include <memory>
#include <variant>

namespace dlt698::model {
/// 已支持的 Data 类型标签，枚举值与线上标签一致。
enum class DataType : std::uint8_t {
    null = 0,
    array = 1,
    structure = 2,
    boolean = 3,
    bit_string = 4,
    int32 = 5,
    uint32 = 6,
    octet_string = 9,
    visible_string = 10,
    utf8_string = 12,
    int8 = 15,
    int16 = 16,
    uint8 = 17,
    uint16 = 18,
    int64 = 20,
    uint64 = 21,
    enumeration = 22,
    float32 = 23,
    float64 = 24,
    date_time = 25,
    date = 26,
    time = 27,
    date_time_s = 28,
    oi = 80,
    oad = 81,
    road = 82,
    omd = 83,
    ti = 84,
    tsa = 85,
    mac = 86,
    rn = 87,
    region = 88,
    scaler_unit = 89,
    rsd = 90,
    csd = 91,
    ms = 92,
    sid = 93,
    sid_mac = 94,
    comdcb = 95,
    rcsd = 96
};

/**
 * @brief 将存储值与协议标签绑定，避免相同 C++ 类型的协议数据混淆。
 * @tparam Tag 协议 Data 类型标签。
 * @tparam T 实际存储类型。
 */
template <DataType Tag, class T>
struct Value {
    static constexpr DataType type = Tag;
    T value{};

    /**
     * @brief 比较相同协议标签下的存储值。
     * @param[in] a 左侧值。
     * @param[in] b 右侧值。
     * @return 底层值相等时返回 true；浮点值遵循 C++ 浮点比较语义。
     */
    friend bool operator==(const Value& a, const Value& b) { return a.value == b.value; }
};

/// 协议 null 类型，无数据内容。
struct Null {
    static constexpr DataType type = DataType::null;

    /**
     * @brief 比较两个无内容的 null 值。
     * @return 始终返回 true。
     */
    friend bool operator==(Null, Null) noexcept { return true; }
};
struct Data;

/// 协议 array，保留元素顺序及每个元素的精确类型。
struct Array {
    static constexpr DataType type = DataType::array;
    std::vector<Data> value;
    /**
     * @brief 递归比较两个数组的元素数量、顺序和内容。
     * @return 对应元素全部相等时返回 true。
     */
    friend bool operator==(const Array&, const Array&);
};

/// 协议 structure，与 array 使用相同存储形式但保持独立标签。
struct Structure {
    static constexpr DataType type = DataType::structure;
    std::vector<Data> value;
    /**
     * @brief 递归比较两个结构的字段数量、顺序和内容。
     * @return 对应字段全部相等时返回 true。
     */
    friend bool operator==(const Structure&, const Structure&);
};

/// 位串：有效位从每个字节的高位开始，末字节未使用的低位须为零。
struct BitString {
    static constexpr DataType type = DataType::bit_string;
    std::size_t bit_count = 0;  ///< 有效位数，不是字节数。
    Bytes value;                ///< 长度须为 ceil(bit_count / 8) 的原始字节。

    /**
     * @brief 比较有效位数及原始字节，包括填充位。
     * @param[in] a 左侧位串。
     * @param[in] b 右侧位串。
     * @return 有效位数和所有字节一致时返回 true。
     */
    friend bool operator==(const BitString& a, const BitString& b) {
        return a.bit_count == b.bit_count && a.value == b.value;
    }
};

using Boolean = Value<DataType::boolean, bool>;
using Int8 = Value<DataType::int8, std::int8_t>;
using Int16 = Value<DataType::int16, std::int16_t>;
using Int32 = Value<DataType::int32, std::int32_t>;
using Int64 = Value<DataType::int64, std::int64_t>;
using UInt8 = Value<DataType::uint8, std::uint8_t>;
using UInt16 = Value<DataType::uint16, std::uint16_t>;
using UInt32 = Value<DataType::uint32, std::uint32_t>;
using UInt64 = Value<DataType::uint64, std::uint64_t>;
using Enum = Value<DataType::enumeration, std::uint8_t>;
using Float32 = Value<DataType::float32, float>;
using Float64 = Value<DataType::float64, double>;
using OctetString = Value<DataType::octet_string, Bytes>;
using VisibleString = Value<DataType::visible_string, std::string>;
using Utf8String = Value<DataType::utf8_string, std::string>;
// 日期时间保留原始日历字段及未指定值（FF/FFFF），不附加时区或进行日期校验。
using DateTime = Value<DataType::date_time, std::array<std::uint8_t, 10>>;
using Date = Value<DataType::date, std::array<std::uint8_t, 5>>;
using Time = Value<DataType::time, std::array<std::uint8_t, 3>>;
using DateTimeS = Value<DataType::date_time_s, std::array<std::uint8_t, 7>>;
using Oi = Value<DataType::oi, std::uint16_t>;

/// 对象属性描述符，编码内容为大端 OI、属性字节和索引。
struct Oad {
    static constexpr DataType type = DataType::oad;
    std::uint16_t oi = 0;
    std::uint8_t attribute = 0;  ///< 包含特征位，保留完整线上字节。
    std::uint8_t index = 0;

    /**
     * @brief 比较对象标识、完整属性字节及索引。
     * @param[in] a 左侧对象属性描述符。
     * @param[in] b 右侧对象属性描述符。
     * @return 所有字段一致时返回 true。
     */
    friend bool operator==(const Oad& a, const Oad& b) {
        return a.oi == b.oi && a.attribute == b.attribute && a.index == b.index;
    }
};

/// 对象方法描述符。
struct Omd {
    static constexpr DataType type = DataType::omd;
    std::uint16_t oi = 0;
    std::uint8_t method = 0;
    std::uint8_t mode = 0;

    /**
     * @brief 比较对象标识、方法和模式字节。
     * @param[in] a 左侧对象方法描述符。
     * @param[in] b 右侧对象方法描述符。
     * @return 所有字段一致时返回 true。
     */
    friend bool operator==(const Omd& a, const Omd& b) {
        return a.oi == b.oi && a.method == b.method && a.mode == b.mode;
    }
};

/// 时间间隔，编解码要求单位编码在 0 至 5 之间。
struct Ti {
    static constexpr DataType type = DataType::ti;
    std::uint8_t unit = 0;
    std::uint16_t interval = 0;

    /**
     * @brief 比较时间单位及间隔值，不进行单位换算。
     * @param[in] a 左侧时间间隔。
     * @param[in] b 右侧时间间隔。
     * @return 两个字段一致时返回 true。
     */
    friend bool operator==(const Ti& a, const Ti& b) {
        return a.unit == b.unit && a.interval == b.interval;
    }
};

/// 终端地址，内容包含地址描述字节和地址字节，不包含 A-XDR 外层长度。
using Tsa = Value<DataType::tsa, Bytes>;
using Mac = Value<DataType::mac, Bytes>;
using Rn = Value<DataType::rn, Bytes>;

struct Sid {
    static constexpr DataType type = DataType::sid;
    std::uint32_t identifier = 0;
    Bytes additional;

    /** @brief 比较安全标识及附加数据。
     * @param[in] a 左侧标识。
     * @param[in] b 右侧标识。
     * @return 字段全部相等时为 true。
     */
    friend bool operator==(const Sid& a, const Sid& b) {
        return a.identifier == b.identifier && a.additional == b.additional;
    }
};

struct SidMac {
    static constexpr DataType type = DataType::sid_mac;
    Sid sid;
    Mac mac;

    /** @brief 比较安全标识和消息鉴别码。
     * @param[in] a 左侧验证码。
     * @param[in] b 右侧验证码。
     * @return 字段全部相等时为 true。
     */
    friend bool operator==(const SidMac& a, const SidMac& b) {
        return a.sid == b.sid && a.mac == b.mac;
    }
};

struct Comdcb {
    static constexpr DataType type = DataType::comdcb;
    std::uint8_t baud = 6;  ///< 0 至 10，或 255 自适应。
    std::uint8_t parity = 0;
    std::uint8_t data_bits = 8;
    std::uint8_t stop_bits = 1;
    std::uint8_t flow_control = 0;

    /** @brief 比较端口通信控制块的全部字段。
     * @param[in] a 左侧配置。
     * @param[in] b 右侧配置。
     * @return 字段全部相等时为 true。
     */
    friend bool operator==(const Comdcb& a, const Comdcb& b) {
        return a.baud == b.baud && a.parity == b.parity && a.data_bits == b.data_bits &&
               a.stop_bits == b.stop_bits && a.flow_control == b.flow_control;
    }
};

/// 十进制倍率及物理单位，工程值换算由调用方完成。
struct ScalerUnit {
    static constexpr DataType type = DataType::scaler_unit;
    std::int8_t scaler = 0;
    std::uint8_t unit = 0;

    /**
     * @brief 比较倍率指数和物理单位编码。
     * @param[in] a 左侧倍率及单位。
     * @param[in] b 右侧倍率及单位。
     * @return 两个字段一致时返回 true。
     */
    friend bool operator==(const ScalerUnit& a, const ScalerUnit& b) {
        return a.scaler == b.scaler && a.unit == b.unit;
    }
};

/// 精确类型数据容器，通过 variant 保留协议标签和对应的拥有型数据。
/** @brief 不可变、拥有内存的记录描述符节点，避免 Data 与 RSD/Region 的递归定义循环。
 * @note 构造和 as 模板在 record.hpp 中定义；复制共享不可变内容，不能经别名修改快照。
 */
class RecordData {
   public:
    /** @brief 从 ROAD/Region/RSD/CSD/MS/RCSD 创建不可变描述符。
     * @tparam T record.hpp 定义的描述符类型。
     * @param[in] value 拥有全部嵌套 Data 内存的值，移动到节点。
     */
    template <class T>
    explicit RecordData(T value);
    /** @brief 获取保存的协议类型。
     * @return 精确的记录描述符 Data 标签；节点已移动时返回 null，但不能再编码。
     */
    DLT698_API DataType type() const;
    /** @brief 按精确类型读取不可变内容。
     * @tparam T 描述符类型。
     * @return 在当前节点生命周期内有效的常量引用。
     * @throws std::bad_variant_access 类型不符或对象已被移动。
     */
    template <class T>
    const T& as() const;
    /** @brief 比较精确描述符类型及所有嵌套字段。
     * @param[in] a 左侧节点。
     * @param[in] b 右侧节点。
     * @return 类型和内容一致为 true，两个已移动节点也相等。
     */
    DLT698_API friend bool operator==(const RecordData& a, const RecordData& b);

   private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
};

struct Data {
    using Payload =
        std::variant<Null, Array, Structure, Boolean, BitString, Int8, Int16, Int32, Int64, UInt8,
                     UInt16, UInt32, UInt64, Enum, Float32, Float64, OctetString, VisibleString,
                     Utf8String, DateTime, Date, Time, DateTimeS, Oi, Oad, Omd, Ti, Tsa, ScalerUnit,
                     RecordData, Mac, Rn, Sid, SidMac, Comdcb>;
    Payload payload = Null{};
    /** @brief 创建协议 null 值。 */
    Data() = default;

    /**
     * @brief 从受支持的协议包装类型创建 Data。
     * @tparam T Payload 支持的包装类型，排除 Data 自身。
     * @param[in] value 转发给 payload 的值；左值复制，右值移动。
     */
    template <class T, std::enable_if_t<!std::is_same_v<std::decay_t<T>, Data>, int> = 0>
    Data(T&& value) : payload(std::forward<T>(value)) {}

    /**
     * @brief 获取当前值的协议类型标签。
     * @return 当前 payload 对应的 DataType。
     */
    DataType type() const {
        return std::visit(
            [](const auto& v) {
                if constexpr (std::is_same_v<std::decay_t<decltype(v)>, RecordData>)
                    return v.type();
                else
                    return std::decay_t<decltype(v)>::type;
            },
            payload);
    }

    /**
     * @brief 按精确协议包装类型访问存储值。
     * @tparam T Payload 中的目标包装类型。
     * @return 存储值的常量引用，生命周期受当前 Data 约束。
     * @throws std::bad_variant_access 当前 payload 不属于 T。
     */
    template <class T>
    const T& as() const {
        return std::get<T>(payload);
    }

    /**
     * @brief 比较协议类型及内容，容器类型递归比较子节点。
     * @param[in] a 左侧数据。
     * @param[in] b 右侧数据。
     * @return 类型和内容相等时返回 true；浮点值遵循 C++ 浮点比较语义。
     */
    friend bool operator==(const Data& a, const Data& b) { return a.payload == b.payload; }
};

/**
 * @brief 按顺序递归比较数组元素。
 * @param[in] a 左侧数组。
 * @param[in] b 右侧数组。
 * @return 元素数量及对应元素均相等时返回 true。
 */
inline bool operator==(const Array& a, const Array& b) { return a.value == b.value; }

/**
 * @brief 按顺序递归比较结构字段。
 * @param[in] a 左侧结构。
 * @param[in] b 右侧结构。
 * @return 字段数量及对应字段均相等时返回 true。
 */
inline bool operator==(const Structure& a, const Structure& b) { return a.value == b.value; }
}  // namespace dlt698::model
