/**
 * @file modbus_client.h
 * @brief Modbus 客户端（Master）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_CLIENT_H
#define EMS_NET_MODBUS_CLIENT_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <cstdint>
#include <modbuspp/master.h>
#include <modbuspp/slave.h>
#include <modbuspp/data.h>

namespace ems {
namespace net {
namespace modbus {

class ModbusClient {
public:
    enum class NetType {
        RTU = 0,
        TCP = 1
    };

    enum class ErrorCode {
        Success = 0,
        ConnectionFailed = -1,
        Timeout = -2,
        InvalidAddress = -3,
        InvalidData = -4,
        SlaveError = -5,
        Unknown = -99
    };

    using ReadCallback = std::function<void(const uint16_t* data, int count)>;
    using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;

    ModbusClient(NetType type, const std::string& connection, const std::string& settings);
    ~ModbusClient();

    bool connect();
    void disconnect();
    bool isConnected() const;

    void setResponseTimeout(int ms);
    void setByteTimeout(int ms);
    void setDebug(bool enable);

    ErrorCode readCoils(int slave_id, int addr, bool* dest, int nb = 1);
    ErrorCode readDiscreteInputs(int slave_id, int addr, bool* dest, int nb = 1);
    ErrorCode readHoldingRegisters(int slave_id, int addr, uint16_t* dest, int nb = 1);
    ErrorCode readInputRegisters(int slave_id, int addr, uint16_t* dest, int nb = 1);

    ErrorCode writeSingleCoil(int slave_id, int addr, bool value);
    ErrorCode writeSingleRegister(int slave_id, int addr, uint16_t value);
    ErrorCode writeMultipleCoils(int slave_id, int addr, const bool* src, int nb);
    ErrorCode writeMultipleRegisters(int slave_id, int addr, const uint16_t* src, int nb);
    ErrorCode writeReadRegisters(int slave_id, int write_addr, const uint16_t* src, int write_nb,
                                  int read_addr, uint16_t* dest, int read_nb);

    bool addSlave(int slave_id);
    bool removeSlave(int slave_id);
    bool hasSlave(int slave_id) const;

    template<typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readData(int slave_id, int addr, Modbus::Data<T, E>& data) {
        int reg_count = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slave_id, addr, data.registers().data(), reg_count);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    template<typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeData(int slave_id, int addr, const Modbus::Data<T, E>& data) {
        int reg_count = sizeof(T) / 2;
        return writeMultipleRegisters(slave_id, addr, data.registers().data(), reg_count);
    }

    int getLastError() const { return last_error_; }
    std::string getLastErrorMsg() const { return last_error_msg_; }

private:
    ErrorCode translateError(int modbus_ret);
    Modbus::Slave* getSlave(int slave_id);

    NetType net_type_;
    Modbus::Master master_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::unique_ptr<Modbus::Slave>> slaves_;
    int last_error_{0};
    std::string last_error_msg_;
    bool connected_{false};
};

template<Modbus::Endian E = Modbus::EndianBig>
class ModbusClientWithData {
public:
    ModbusClientWithData(typename ModbusClient::NetType type, const std::string& connection,
                         const std::string& settings, int slave_id = 1)
        : client_(type, connection, settings), slave_id_(slave_id) {}

    bool connect() {
        return client_.connect();
    }

    void disconnect() { client_.disconnect(); }
    bool isConnected() const { return client_.isConnected(); }

    void setSlaveId(int slave_id) { slave_id_ = slave_id; }
    int getSlaveId() const { return slave_id_; }

    typename ModbusClient::ErrorCode readInt16(int addr, int16_t& value) {
        uint16_t raw;
        auto ret = client_.readHoldingRegisters(slave_id_, addr, &raw, 1);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = static_cast<int16_t>(raw);
        }
        return ret;
    }

    typename ModbusClient::ErrorCode readUInt16(int addr, uint16_t& value) {
        return client_.readHoldingRegisters(slave_id_, addr, &value, 1);
    }

    typename ModbusClient::ErrorCode readInt32(int addr, int32_t& value) {
        Modbus::Data<int32_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusClient::ErrorCode readUInt32(int addr, uint32_t& value) {
        Modbus::Data<uint32_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusClient::ErrorCode readFloat(int addr, float& value) {
        Modbus::Data<float, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusClient::ErrorCode readDouble(int addr, double& value) {
        Modbus::Data<double, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusClient::ErrorCode readInt64(int addr, int64_t& value) {
        Modbus::Data<int64_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusClient::ErrorCode writeInt16(int addr, int16_t value) {
        return client_.writeSingleRegister(slave_id_, addr, static_cast<uint16_t>(value));
    }

    typename ModbusClient::ErrorCode writeUInt16(int addr, uint16_t value) {
        return client_.writeSingleRegister(slave_id_, addr, value);
    }

    typename ModbusClient::ErrorCode writeInt32(int addr, int32_t value) {
        Modbus::Data<int32_t, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    typename ModbusClient::ErrorCode writeUInt32(int addr, uint32_t value) {
        Modbus::Data<uint32_t, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    typename ModbusClient::ErrorCode writeFloat(int addr, float value) {
        Modbus::Data<float, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    typename ModbusClient::ErrorCode writeDouble(int addr, double value) {
        Modbus::Data<double, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    ModbusClient& client() { return client_; }

private:
    ModbusClient client_;
    int slave_id_;
};

} // namespace modbus
} // namespace net
} // namespace ems

#endif // EMS_NET_MODBUS_CLIENT_H
