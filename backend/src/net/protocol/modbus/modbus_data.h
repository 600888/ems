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

namespace ems {
namespace net {
namespace modbus {

class ModbusDataInt16 {
   public:
    ModbusDataInt16() : value_(0) {}
    explicit ModbusDataInt16(int16_t v) : value_(v) {}

    int16_t value() const { return value_; }
    void setValue(int16_t v) { value_ = v; }
    uint16_t* registers() { return reinterpret_cast<uint16_t*>(&value_); }
    const uint16_t* registers() const { return reinterpret_cast<const uint16_t*>(&value_); }
    static constexpr size_t registerCount() { return 1; }

    void updateValue() {}
    void updateRegisters() {}

   private:
    int16_t value_;
};

class ModbusDataUInt16 {
   public:
    ModbusDataUInt16() : value_(0) {}
    explicit ModbusDataUInt16(uint16_t v) : value_(v) {}

    uint16_t value() const { return value_; }
    void setValue(uint16_t v) { value_ = v; }
    uint16_t* registers() { return &value_; }
    const uint16_t* registers() const { return &value_; }
    static constexpr size_t registerCount() { return 1; }

    void updateValue() {}
    void updateRegisters() {}

   private:
    uint16_t value_;
};

template <typename T, Modbus::Endian E = Modbus::EndianBig>
class ModbusDataWrapper {
   public:
    ModbusDataWrapper() : data_() {}
    explicit ModbusDataWrapper(T v) : data_(v) {}

    T value() const { return data_.value(); }
    void setValue(T v) { data_ = v; }
    uint16_t* registers() { return data_.registers().data(); }
    const uint16_t* registers() const { return data_.registers().data(); }
    static constexpr size_t registerCount() { return sizeof(T) / 2; }

    void updateValue() { data_.updateValue(); }
    void updateRegisters() { data_.updateRegisters(); }

    uint16_t toRegister() const { return registers()[0]; }
    void fromRegister(uint16_t reg) { registers()[0] = reg; }

    Modbus::Data<T, E>& data() { return data_; }
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
    ModbusRegister() : value_(0) {}
    explicit ModbusRegister(uint16_t v) : value_(v) {}

    uint16_t value8() const { return value_; }
    int16_t valueS16() const { return static_cast<int16_t>(value_); }
    float toFloat(Modbus::Endian e = Modbus::EndianBig) const;
    double toDouble(Modbus::Endian e = Modbus::EndianBig) const;

    void setValue(uint16_t v) { value_ = v; }
    void setFromFloat(float v, Modbus::Endian e = Modbus::EndianBig);
    void setFromDouble(double v, Modbus::Endian e = Modbus::EndianBig);

    uint16_t* registers() { return &value_; }
    const uint16_t* registers() const { return &value_; }
    static constexpr size_t registerCount() { return 1; }

   private:
    uint16_t value_;
};

class ModbusRegister32 {
   public:
    ModbusRegister32() { value_[0] = value_[1] = 0; }

    uint32_t valueU32(Modbus::Endian e = Modbus::EndianBig) const;
    int32_t valueS32(Modbus::Endian e = Modbus::EndianBig) const;
    float valueFloat(Modbus::Endian e = Modbus::EndianBig) const;

    void setValue(uint32_t v, Modbus::Endian e = Modbus::EndianBig);
    void setValue(int32_t v, Modbus::Endian e = Modbus::EndianBig);
    void setValue(float v, Modbus::Endian e = Modbus::EndianBig);

    uint16_t* registers() { return value_; }
    const uint16_t* registers() const { return value_; }
    static constexpr size_t registerCount() { return 2; }

   private:
    uint16_t value_[2];
};

class ModbusRegister64 {
   public:
    ModbusRegister64() { value_[0] = value_[1] = value_[2] = value_[3] = 0; }

    uint64_t valueU64(Modbus::Endian e = Modbus::EndianBig) const;
    int64_t valueS64(Modbus::Endian e = Modbus::EndianBig) const;
    double valueDouble(Modbus::Endian e = Modbus::EndianBig) const;

    void setValue(uint64_t v, Modbus::Endian e = Modbus::EndianBig);
    void setValue(int64_t v, Modbus::Endian e = Modbus::EndianBig);
    void setValue(double v, Modbus::Endian e = Modbus::EndianBig);

    uint16_t* registers() { return value_; }
    const uint16_t* registers() const { return value_; }
    static constexpr size_t registerCount() { return 4; }

   private:
    uint16_t value_[4];
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_DATA_H
