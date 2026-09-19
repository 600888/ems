/**
 * @file modbus_server.h
 * @brief Modbus 服务端（Server）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_SERVER_H
#define EMS_NET_MODBUS_SERVER_H

#include <modbuspp/bufferedslave.h>
#include <modbuspp/server.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ems {
namespace net {
namespace modbus {

class ModbusServer {
   public:
    enum class NetType { RTU = 0, TCP = 1 };

    enum class ErrorCode {
        Success = 0,
        BindFailed = -1,
        StartFailed = -2,
        AlreadyRunning = -3,
        Unknown = -99
    };

    using DataChangeCallback =
        std::function<void(int slaveId, Modbus::Table table, int addr, int nb)>;

    ModbusServer(NetType type, const std::string& connection, const std::string& settings);
    ~ModbusServer();

    bool start();
    void stop();
    bool isRunning() const;
    void join();

    int addSlave(int slaveId);
    bool removeSlave(int slaveId);
    bool hasSlave(int slaveId) const;

    bool setHoldingRegisterBlock(int slaveId, int startAddr, int count);
    bool setInputRegisterBlock(int slaveId, int startAddr, int count);
    bool setCoilBlock(int slaveId, int startAddr, int count);
    bool setDiscreteInputBlock(int slaveId, int startAddr, int count);

    ErrorCode writeHoldingRegister(int slaveId, int addr, uint16_t value);
    ErrorCode writeHoldingRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    ErrorCode writeInputRegister(int slaveId, int addr, uint16_t value);
    ErrorCode writeInputRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    ErrorCode writeCoil(int slaveId, int addr, bool value);
    ErrorCode writeCoils(int slaveId, int addr, const bool* src, int nb);

    ErrorCode readHoldingRegister(int slaveId, int addr, uint16_t& value);
    ErrorCode readHoldingRegisters(int slaveId, int addr, uint16_t* dest, int nb);
    ErrorCode readInputRegister(int slaveId, int addr, uint16_t& value);
    ErrorCode readInputRegisters(int slaveId, int addr, uint16_t* dest, int nb);
    ErrorCode readCoil(int slaveId, int addr, bool& value);
    ErrorCode readCoils(int slaveId, int addr, bool* dest, int nb);
    ErrorCode readDiscreteInput(int slaveId, int addr, bool& value);
    ErrorCode readDiscreteInputs(int slaveId, int addr, bool* dest, int nb);

    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeHoldingData(int slaveId, int addr, const Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        return writeHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
    }

    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readHoldingData(int slaveId, int addr, Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    void setDataChangeCallback(DataChangeCallback cb) { dataChangeCallback_ = cb; }

   private:
    void runLoop();
    static int messageHandler(Modbus::Message* req, Modbus::Device* dev);

    NetType netType_;
    Modbus::Server server_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::shared_ptr<Modbus::BufferedSlave>> slaves_;
    std::unique_ptr<std::thread> serverThread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    DataChangeCallback dataChangeCallback_;
};

template <Modbus::Endian E = Modbus::EndianBig>
class ModbusServerWithData {
   public:
    ModbusServerWithData(typename ModbusServer::NetType type, const std::string& connection,
                         const std::string& settings)
        : server_(type, connection, settings) {}

    bool start() { return server_.start(); }

    void stop() { server_.stop(); }
    bool isRunning() const { return server_.isRunning(); }
    void join() { server_.join(); }

    int addSlave(int slaveId) { return server_.addSlave(slaveId); }
    bool removeSlave(int slaveId) { return server_.removeSlave(slaveId); }

    bool setHoldingRegisterBlock(int slaveId, int startAddr, int count) {
        return server_.setHoldingRegisterBlock(slaveId, startAddr, count);
    }

    typename ModbusServer::ErrorCode writeInt16(int slaveId, int addr, int16_t value) {
        return server_.writeHoldingRegister(slaveId, addr, static_cast<uint16_t>(value));
    }

    typename ModbusServer::ErrorCode writeUInt16(int slaveId, int addr, uint16_t value) {
        return server_.writeHoldingRegister(slaveId, addr, value);
    }

    typename ModbusServer::ErrorCode writeInt32(int slaveId, int addr, int32_t value) {
        Modbus::Data<int32_t, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    typename ModbusServer::ErrorCode writeUInt32(int slaveId, int addr, uint32_t value) {
        Modbus::Data<uint32_t, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    typename ModbusServer::ErrorCode writeFloat(int slaveId, int addr, float value) {
        Modbus::Data<float, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    typename ModbusServer::ErrorCode writeDouble(int slaveId, int addr, double value) {
        Modbus::Data<double, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    typename ModbusServer::ErrorCode readInt16(int slaveId, int addr, int16_t& value) {
        uint16_t raw;
        auto ret = server_.readHoldingRegister(slaveId, addr, raw);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = static_cast<int16_t>(raw);
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readUInt16(int slaveId, int addr, uint16_t& value) {
        return server_.readHoldingRegister(slaveId, addr, value);
    }

    typename ModbusServer::ErrorCode readInt32(int slaveId, int addr, int32_t& value) {
        Modbus::Data<int32_t, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readUInt32(int slaveId, int addr, uint32_t& value) {
        Modbus::Data<uint32_t, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readFloat(int slaveId, int addr, float& value) {
        Modbus::Data<float, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readDouble(int slaveId, int addr, double& value) {
        Modbus::Data<double, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    ModbusServer& server() { return server_; }

   private:
    ModbusServer server_;
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_SERVER_H
