/**
 * @file modbus_handler.h
 * @brief Modbus 协议处理器声明
 * @author cdy
 * @date 2026-04-08
 */
#ifndef EMS_NET_DEVICE_PROTOCOL_HANDLER_MODBUS_H
#define EMS_NET_DEVICE_PROTOCOL_HANDLER_MODBUS_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <functional>
#include <future>
#include <chrono>
#include <unordered_set>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <variant>
#include <type_traits>
#include <cstdint>
#include "base_handler.h"
#include "../../protocol/modbus/modbus_server.h"
#include "../../protocol/modbus/modbus_client.h"

namespace ems {
namespace net {
namespace device {

class CoilBits {
public:
    CoilBits() = default;
    explicit CoilBits(size_t count) : data_(count > 0 ? (count + 7) / 8 : 0), count_(count) {}

    void resize(size_t count) {
        count_ = count;
        data_.resize(count > 0 ? (count + 7) / 8 : 0);
    }

    size_t size() const { return count_; }

    bool operator[](size_t index) const {
        if (index >= count_) return false;
        return (data_[index / 8] & (1 << (index % 8))) != 0;
    }

    void set(size_t index, bool value) {
        if (index >= count_) return;
        if (value) {
            data_[index / 8] |= (1 << (index % 8));
        } else {
            data_[index / 8] &= ~(1 << (index % 8));
        }
    }

    bool* data() {
        return reinterpret_cast<bool*>(data_.data());
    }

    const bool* data() const {
        return reinterpret_cast<const bool*>(data_.data());
    }

private:
    std::vector<uint8_t> data_;
    size_t count_ = 0;
};

class ModbusWriteCallback {
public:
    virtual ~ModbusWriteCallback() = default;
    virtual void onModbusClientWrite(int slaveId, int funcCode, int address,
                                         const std::vector<int>& values) = 0;
};

class ModbusServerHandler : public ServerHandler {
public:
    using Logger = std::function<void(const std::string&)>;

    ModbusServerHandler();
    explicit ModbusServerHandler(Logger logger);
    ~ModbusServerHandler() override;

    void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    std::future<bool> start() override;
    std::future<bool> stop() override;

    ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) override;
    bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) override;
    void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) override;

    ValueType getValueByAddress(int funcCode, int slaveId, int address) override;
    void setValueByAddress(int funcCode, int slaveId, int address,
                               const ValueType& value) override;

    ValueType getValueByAddressWithDecode(int funcCode, int slaveId, int address,
                                               const std::string& decode);
    void setValueByAddressWithDecode(int funcCode, int slaveId, int address,
                                          const ValueType& value, const std::string& decode);

    std::shared_ptr<modbus::ModbusServer> server() const { return server_; }

    virtual std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const override;
    virtual void clearCapturedMessages() override;
    virtual AvgTimeStats getAvgTime() const override;

    void setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback);
    std::shared_ptr<modbus::ModbusServer> getServer() const { return server_; }

private:
    void onModbusWriteInternal(int slaveId, int funcCode, int address,
                                    const std::vector<int>& values);
    int parseAddress(const std::string& addressStr) const;
    int getRegisterCount(const std::string& decode) const;
    bool valueToInt(const ValueType& value, int& result) const;
    std::shared_ptr<modbus::ModbusServer> server_;

    Logger logger_;
    std::vector<std::shared_ptr<ProtocolPoint>> points_;
    std::vector<int> slaveIdList_;
    std::unordered_set<std::shared_ptr<ProtocolPoint>> pointsSet_;
    std::mutex mutex_;
    std::shared_ptr<ModbusWriteCallback> writeCallback_;
    std::atomic<bool> serverStarted_{false};

    modbus::ModbusServer::NetType netType_;
    std::string connection_;
    std::string settings_;
    int port_{502};
};

class ModbusClientHandler : public ClientHandler {
public:
    using Logger = std::function<void(const std::string&)>;

    ModbusClientHandler();
    explicit ModbusClientHandler(Logger logger);
    ~ModbusClientHandler() override;

    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    virtual std::future<bool> start() override;
    virtual std::future<bool> stop() override;

    virtual bool connect() override;
    virtual void disconnect() override;
    virtual bool isConnected() const override;

    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) override;
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) override;
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) override;
    
    virtual std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const override;
    virtual void clearCapturedMessages() override;
    virtual AvgTimeStats getAvgTime() const override;

    std::vector<int> readRegistersBatchAsync(int funcCode, int slaveId,
                                                  int startAddress, int count);
    std::vector<int> readHoldingRegisters(int slaveId, int startAddress, int count);
    std::vector<int> readInputRegisters(int slaveId, int startAddress, int count);
    CoilBits readCoils(int slaveId, int startAddress, int count);
    CoilBits readDiscreteInputs(int slaveId, int startAddress, int count);

    bool writeSingleRegister(int slaveId, int address, int value);
    bool writeMultipleRegisters(int slaveId, int startAddress,
                                    const std::vector<int>& values);
    bool writeSingleCoil(int slaveId, int address, bool value);
    bool writeMultipleCoils(int slaveId, int startAddress,
                               const CoilBits& values);

    std::shared_ptr<modbus::ModbusClient> client() const { return client_; }

private:
    int extractBitValue(int value, int bit_offset) const;
    int applyBitValue(int current_value, int bit_offset, bool bit_value) const;
    ValueType readValueInternal(int funcCode, int slaveId, int address,
                                   const std::string& decode);
    int parseAddress(const std::string& addressStr) const;
    int getRegisterCount(const std::string& decode) const;
    bool valueToInt(const ValueType& value, int& result) const;

    std::shared_ptr<modbus::ModbusClient> client_;
    Logger logger_;
    std::vector<std::shared_ptr<ProtocolPoint>> points_;
    std::unordered_set<std::shared_ptr<ProtocolPoint>> pointsSet_;
    std::mutex mutex_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> clientInitialized_{false};

    modbus::ModbusClient::NetType netType_;
    std::string connection_;
    std::string settings_;
    int defaultSlaveId_{1};
    int responseTimeoutMs_{1000};
    int byteTimeoutMs_{1000};
};

} // namespace device
} // namespace net
} // namespace ems

#endif // EMS_NET_DEVICE_PROTOCOL_HANDLER_MODBUS_H
