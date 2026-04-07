/**
 * @file modbus_server.h
 * @brief Modbus 服务端（Server）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_SERVER_H
#define EMS_NET_MODBUS_SERVER_H

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <modbuspp/server.h>
#include <modbuspp/bufferedslave.h>

namespace ems {
namespace net {
namespace modbus {

class ModbusServer {
public:
    enum class NetType {
        RTU = 0,
        TCP = 1
    };

    enum class ErrorCode {
        Success = 0,
        BindFailed = -1,
        StartFailed = -2,
        AlreadyRunning = -3,
        Unknown = -99
    };

    using DataChangeCallback = std::function<void(int slave_id, Modbus::Table table, int addr, int nb)>;

    ModbusServer(NetType type, const std::string& connection, const std::string& settings);
    ~ModbusServer();

    bool start();
    void stop();
    bool isRunning() const;
    void join();

    int addSlave(int slave_id);
    bool removeSlave(int slave_id);
    bool hasSlave(int slave_id) const;

    bool setHoldingRegisterBlock(int slave_id, int start_addr, int count);
    bool setInputRegisterBlock(int slave_id, int start_addr, int count);
    bool setCoilBlock(int slave_id, int start_addr, int count);
    bool setDiscreteInputBlock(int slave_id, int start_addr, int count);

    ErrorCode writeHoldingRegister(int slave_id, int addr, uint16_t value);
    ErrorCode writeHoldingRegisters(int slave_id, int addr, const uint16_t* src, int nb);
    ErrorCode writeInputRegister(int slave_id, int addr, uint16_t value);
    ErrorCode writeInputRegisters(int slave_id, int addr, const uint16_t* src, int nb);
    ErrorCode writeCoil(int slave_id, int addr, bool value);
    ErrorCode writeCoils(int slave_id, int addr, const bool* src, int nb);

    ErrorCode readHoldingRegister(int slave_id, int addr, uint16_t& value);
    ErrorCode readHoldingRegisters(int slave_id, int addr, uint16_t* dest, int nb);
    ErrorCode readInputRegister(int slave_id, int addr, uint16_t& value);
    ErrorCode readInputRegisters(int slave_id, int addr, uint16_t* dest, int nb);
    ErrorCode readCoil(int slave_id, int addr, bool& value);
    ErrorCode readCoils(int slave_id, int addr, bool* dest, int nb);
    ErrorCode readDiscreteInput(int slave_id, int addr, bool& value);
    ErrorCode readDiscreteInputs(int slave_id, int addr, bool* dest, int nb);

    template<typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeHoldingData(int slave_id, int addr, const Modbus::Data<T, E>& data) {
        int reg_count = sizeof(T) / 2;
        return writeHoldingRegisters(slave_id, addr, data.registers().data(), reg_count);
    }

    template<typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readHoldingData(int slave_id, int addr, Modbus::Data<T, E>& data) {
        int reg_count = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slave_id, addr, data.registers().data(), reg_count);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    void setDataChangeCallback(DataChangeCallback cb) { data_change_callback_ = cb; }

private:
    void runLoop();
    static int messageHandler(Modbus::Message* req, Modbus::Device* dev);

    NetType net_type_;
    Modbus::Server server_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::shared_ptr<Modbus::BufferedSlave>> slaves_;
    std::unique_ptr<std::thread> server_thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    DataChangeCallback data_change_callback_;
};

template<Modbus::Endian E = Modbus::EndianBig>
class ModbusServerWithData {
public:
    ModbusServerWithData(typename ModbusServer::NetType type, const std::string& connection,
                          const std::string& settings)
        : server_(type, connection, settings) {}

    bool start() {
        return server_.start();
    }

    void stop() { server_.stop(); }
    bool isRunning() const { return server_.isRunning(); }
    void join() { server_.join(); }

    int addSlave(int slave_id) { return server_.addSlave(slave_id); }
    bool removeSlave(int slave_id) { return server_.removeSlave(slave_id); }

    bool setHoldingRegisterBlock(int slave_id, int start_addr, int count) {
        return server_.setHoldingRegisterBlock(slave_id, start_addr, count);
    }

    typename ModbusServer::ErrorCode writeInt16(int slave_id, int addr, int16_t value) {
        return server_.writeHoldingRegister(slave_id, addr, static_cast<uint16_t>(value));
    }

    typename ModbusServer::ErrorCode writeUInt16(int slave_id, int addr, uint16_t value) {
        return server_.writeHoldingRegister(slave_id, addr, value);
    }

    typename ModbusServer::ErrorCode writeInt32(int slave_id, int addr, int32_t value) {
        Modbus::Data<int32_t, E> data(value);
        return server_.writeHoldingData(slave_id, addr, data);
    }

    typename ModbusServer::ErrorCode writeUInt32(int slave_id, int addr, uint32_t value) {
        Modbus::Data<uint32_t, E> data(value);
        return server_.writeHoldingData(slave_id, addr, data);
    }

    typename ModbusServer::ErrorCode writeFloat(int slave_id, int addr, float value) {
        Modbus::Data<float, E> data(value);
        return server_.writeHoldingData(slave_id, addr, data);
    }

    typename ModbusServer::ErrorCode writeDouble(int slave_id, int addr, double value) {
        Modbus::Data<double, E> data(value);
        return server_.writeHoldingData(slave_id, addr, data);
    }

    typename ModbusServer::ErrorCode readInt16(int slave_id, int addr, int16_t& value) {
        uint16_t raw;
        auto ret = server_.readHoldingRegister(slave_id, addr, raw);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = static_cast<int16_t>(raw);
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readUInt16(int slave_id, int addr, uint16_t& value) {
        return server_.readHoldingRegister(slave_id, addr, value);
    }

    typename ModbusServer::ErrorCode readInt32(int slave_id, int addr, int32_t& value) {
        Modbus::Data<int32_t, E> data;
        auto ret = server_.readHoldingData(slave_id, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readUInt32(int slave_id, int addr, uint32_t& value) {
        Modbus::Data<uint32_t, E> data;
        auto ret = server_.readHoldingData(slave_id, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readFloat(int slave_id, int addr, float& value) {
        Modbus::Data<float, E> data;
        auto ret = server_.readHoldingData(slave_id, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    typename ModbusServer::ErrorCode readDouble(int slave_id, int addr, double& value) {
        Modbus::Data<double, E> data;
        auto ret = server_.readHoldingData(slave_id, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    ModbusServer& server() { return server_; }

private:
    ModbusServer server_;
};

} // namespace modbus
} // namespace net
} // namespace ems

#endif // EMS_NET_MODBUS_SERVER_H
