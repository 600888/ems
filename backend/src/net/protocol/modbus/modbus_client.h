/**
 * @file modbus_client.h
 * @brief Modbus 客户端（Master）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_CLIENT_H
#define EMS_NET_MODBUS_CLIENT_H

#include <modbuspp/data.h>
#include <modbuspp/master.h>
#include <modbuspp/slave.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ems {
namespace net {
namespace modbus {

class ModbusClient {
   public:
    enum class NetType { RTU = 0, TCP = 1 };

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

    ErrorCode readCoils(int slaveId, int addr, bool* dest, int nb = 1);
    ErrorCode readDiscreteInputs(int slaveId, int addr, bool* dest, int nb = 1);
    ErrorCode readHoldingRegisters(int slaveId, int addr, uint16_t* dest, int nb = 1);
    ErrorCode readInputRegisters(int slaveId, int addr, uint16_t* dest, int nb = 1);

    ErrorCode writeSingleCoil(int slaveId, int addr, bool value);
    ErrorCode writeSingleRegister(int slaveId, int addr, uint16_t value);
    ErrorCode writeMultipleCoils(int slaveId, int addr, const bool* src, int nb);
    ErrorCode writeMultipleRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    ErrorCode writeReadRegisters(int slaveId, int writeAddr, const uint16_t* src, int writeNb,
                                 int readAddr, uint16_t* dest, int readNb);

    bool addSlave(int slaveId);
    bool removeSlave(int slaveId);
    bool hasSlave(int slaveId) const;

    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readData(int slaveId, int addr, Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeData(int slaveId, int addr, const Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        return writeMultipleRegisters(slaveId, addr, data.registers().data(), regCount);
    }

    int getLastError() const { return lastError_; }
    std::string getLastErrorMsg() const { return lastErrorMsg_; }

   private:
    ErrorCode translateError(int modbusRet);
    Modbus::Slave* getSlave(int slaveId);

    NetType netType_;
    Modbus::Master master_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::unique_ptr<Modbus::Slave>> slaves_;
    int lastError_{0};
    std::string lastErrorMsg_;
    bool connected_{false};
};

template <Modbus::Endian E = Modbus::EndianBig>
class ModbusClientWithData {
   public:
    ModbusClientWithData(typename ModbusClient::NetType type, const std::string& connection,
                         const std::string& settings, int slaveId = 1)
        : client_(type, connection, settings), slave_id_(slaveId) {}

    bool connect() { return client_.connect(); }

    void disconnect() { client_.disconnect(); }
    bool isConnected() const { return client_.isConnected(); }

    void setSlaveId(int slaveId) { slave_id_ = slaveId; }
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

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_CLIENT_H
