/**
 * @file modbus_data.h
 * @brief Modbus 数据类型封装
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_DATA_H
#define EMS_NET_MODBUS_DATA_H

#include <modbuspp/data.h>

#include <array>
#include <cstdint>

#include "net/protocol/modbus/modbus_limits.h"

namespace ems {
namespace net {
namespace modbus {

class ModbusDataInt16 {
   public:
    /** @brief 构造 ModbusDataInt16 实例。 */
    ModbusDataInt16() : value_(0) {}

    /**
     * @brief 构造 ModbusDataInt16 实例。
     * @param[in] v 待设置的数值。
     */
    explicit ModbusDataInt16(int16_t v) : value_(v) {}

    /**
     * @brief 获取当前数值。
     * @return 按所选类型解释的当前数值。
     */
    int16_t value() const { return value_; }

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     */
    void setValue(int16_t v) { value_ = v; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return reinterpret_cast<uint16_t*>(&value_); }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return reinterpret_cast<const uint16_t*>(&value_); }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() { return enum_value(Layout::SingleRegisters); }

    /** @brief 根据寄存器内容更新解码值。 */
    void updateValue() {}

    /** @brief 根据当前值更新寄存器内容。 */
    void updateRegisters() {}

   private:
    int16_t value_;
};

class ModbusDataUInt16 {
   public:
    /** @brief 构造 ModbusDataUInt16 实例。 */
    ModbusDataUInt16() : value_(0) {}

    /**
     * @brief 构造 ModbusDataUInt16 实例。
     * @param[in] v 待设置的数值。
     */
    explicit ModbusDataUInt16(uint16_t v) : value_(v) {}

    /**
     * @brief 获取当前数值。
     * @return 按所选类型解释的当前数值。
     */
    uint16_t value() const { return value_; }

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     */
    void setValue(uint16_t v) { value_ = v; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return &value_; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return &value_; }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() { return enum_value(Layout::SingleRegisters); }

    /** @brief 根据寄存器内容更新解码值。 */
    void updateValue() {}

    /** @brief 根据当前值更新寄存器内容。 */
    void updateRegisters() {}

   private:
    uint16_t value_;
};

template <typename T, Modbus::Endian E = Modbus::EndianBig>
class ModbusDataWrapper {
   public:
    /** @brief 构造 ModbusDataWrapper 实例。 */
    ModbusDataWrapper() : data_() {}

    /**
     * @brief 构造 ModbusDataWrapper 实例。
     * @param[in] v 待设置的数值。
     */
    explicit ModbusDataWrapper(T v) : data_(v) {}

    /**
     * @brief 获取当前数值。
     * @return 按所选类型解释的当前数值。
     */
    T value() const { return data_.value(); }

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     */
    void setValue(T v) { data_ = v; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return data_.registers().data(); }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return data_.registers().data(); }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() {
        return sizeof(T) / enum_value(Layout::BytesPerRegister);
    }

    /** @brief 根据寄存器内容更新解码值。 */
    void updateValue() { data_.updateValue(); }

    /** @brief 根据当前值更新寄存器内容。 */
    void updateRegisters() { data_.updateRegisters(); }

    /**
     * @brief 获取首个寄存器的值。
     * @return 按指定类型解释的数值。
     */
    uint16_t toRegister() const { return registers()[0]; }

    /**
     * @brief 从单个寄存器设置数据。
     * @param[in] reg 寄存器数值。
     */
    void fromRegister(uint16_t reg) { registers()[0] = reg; }

    /**
     * @brief 获取底层 Modbus 数据包装对象。
     * @return 对应对象的引用。
     */
    Modbus::Data<T, E>& data() { return data_; }

    /**
     * @brief 获取底层 Modbus 数据包装对象。
     * @return 对应对象的引用。
     */
    const Modbus::Data<T, E>& data() const { return data_; }

   private:
    Modbus::Data<T, E> data_;
};

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusInt32 = ModbusDataWrapper<int32_t, E>;

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusUInt32 = ModbusDataWrapper<uint32_t, E>;

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusFloat = ModbusDataWrapper<float, E>;

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusDouble = ModbusDataWrapper<double, E>;

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusInt64 = ModbusDataWrapper<int64_t, E>;

template <Modbus::Endian E = Modbus::EndianBig>
using ModbusUInt64 = ModbusDataWrapper<uint64_t, E>;

class ModbusRegister {
   public:
    /** @brief 构造 ModbusRegister 实例。 */
    ModbusRegister() : value_(0) {}

    /**
     * @brief 构造 ModbusRegister 实例。
     * @param[in] v 待设置的数值。
     */
    explicit ModbusRegister(uint16_t v) : value_(v) {}

    /**
     * @brief 获取寄存器的无符号数值。
     * @return 按所选类型解释的当前数值。
     */
    uint16_t value8() const { return value_; }

    /**
     * @brief 将寄存器解释为有符号 16 位整数。
     * @return 按所选类型解释的当前数值。
     */
    int16_t valueS16() const { return static_cast<int16_t>(value_); }

    /**
     * @brief 按指定字节序转换为单精度浮点数。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按指定类型解释的数值。
     */
    float toFloat(Modbus::Endian e = Modbus::EndianBig) const;
    /**
     * @brief 按指定字节序转换为双精度浮点数。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按指定类型解释的数值。
     */
    double toDouble(Modbus::Endian e = Modbus::EndianBig) const;

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     */
    void setValue(uint16_t v) { value_ = v; }

    /**
     * @brief 按指定字节序写入单精度浮点数。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setFromFloat(float v, Modbus::Endian e = Modbus::EndianBig);
    /**
     * @brief 按指定字节序写入双精度浮点数。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setFromDouble(double v, Modbus::Endian e = Modbus::EndianBig);

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return &value_; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return &value_; }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() { return enum_value(Layout::SingleRegisters); }

   private:
    uint16_t value_;
};

class ModbusRegister32 {
   public:
    /** @brief 构造 ModbusRegister32 实例。 */
    ModbusRegister32() : value_{} {}

    /**
     * @brief 按指定字节序读取 U32 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    uint32_t valueU32(Modbus::Endian e = Modbus::EndianBig) const;
    /**
     * @brief 按指定字节序读取 S32 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    int32_t valueS32(Modbus::Endian e = Modbus::EndianBig) const;
    /**
     * @brief 按指定字节序读取 Float 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    float valueFloat(Modbus::Endian e = Modbus::EndianBig) const;

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(uint32_t v, Modbus::Endian e = Modbus::EndianBig);
    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(int32_t v, Modbus::Endian e = Modbus::EndianBig);
    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(float v, Modbus::Endian e = Modbus::EndianBig);

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return value_; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return value_; }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() { return enum_value(Layout::DoubleRegisters); }

   private:
    uint16_t value_[enum_value(Layout::DoubleRegisters)];
};

class ModbusRegister64 {
   public:
    /** @brief 构造 ModbusRegister64 实例。 */
    ModbusRegister64() : value_{} {}

    /**
     * @brief 按指定字节序读取 U64 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    uint64_t valueU64(Modbus::Endian e = Modbus::EndianBig) const;
    /**
     * @brief 按指定字节序读取 S64 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    int64_t valueS64(Modbus::Endian e = Modbus::EndianBig) const;
    /**
     * @brief 按指定字节序读取 Double 数值。
     * @param[in] e Modbus 寄存器字节序。
     * @return 按所选类型解释的当前数值。
     */
    double valueDouble(Modbus::Endian e = Modbus::EndianBig) const;

    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(uint64_t v, Modbus::Endian e = Modbus::EndianBig);
    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(int64_t v, Modbus::Endian e = Modbus::EndianBig);
    /**
     * @brief 设置当前数值。
     * @param[in] v 待设置的数值。
     * @param[in] e Modbus 寄存器字节序。
     */
    void setValue(double v, Modbus::Endian e = Modbus::EndianBig);

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    uint16_t* registers() { return value_; }

    /**
     * @brief 获取底层寄存器缓冲区。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const uint16_t* registers() const { return value_; }

    /**
     * @brief 获取该数据类型占用的寄存器数量。
     * @return 该数据类型占用的 16 位寄存器数量。
     */
    static constexpr size_t registerCount() { return enum_value(Layout::QuadRegisters); }

   private:
    uint16_t value_[enum_value(Layout::QuadRegisters)];
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_DATA_H
