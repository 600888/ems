/**
 * @file modbus_handler.h
 * @brief Modbus 协议处理器实现
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

template<typename PointT>
class ModbusServerHandler : public ServerHandler<PointT> {
public:
    using Logger = std::function<void(const std::string&)>;

    ModbusServerHandler();
    explicit ModbusServerHandler(Logger logger);
    ~ModbusServerHandler() override;

    void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    std::future<bool> start() override;
    std::future<bool> stop() override;

    ValueType readValue(const std::shared_ptr<PointT>& point) override;
    bool writeValue(const std::shared_ptr<PointT>& point, const ValueType& value) override;
    void addPoints(const std::vector<std::shared_ptr<PointT>>& points) override;

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
    std::vector<std::shared_ptr<PointT>> points_;
    std::vector<int> slaveIdList_;
    std::unordered_set<std::shared_ptr<PointT>> pointsSet_;
    std::mutex mutex_;
    std::shared_ptr<ModbusWriteCallback> writeCallback_;
    std::atomic<bool> serverStarted_{false};

    modbus::ModbusServer::NetType netType_;
    std::string connection_;
    std::string settings_;
    int port_{502};
};

template<typename PointT>
class ModbusClientHandler : public ClientHandler<PointT> {
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

    virtual ValueType readValue(const std::shared_ptr<PointT>& point) override;
    virtual bool writeValue(const std::shared_ptr<PointT>& point, const ValueType& value) override;
    virtual void addPoints(const std::vector<std::shared_ptr<PointT>>& points) override;
    
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
    std::vector<std::shared_ptr<PointT>> points_;
    std::unordered_set<std::shared_ptr<PointT>> pointsSet_;
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

template<typename PointT>
ModbusServerHandler<PointT>::ModbusServerHandler()
    : netType_(modbus::ModbusServer::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

template<typename PointT>
ModbusServerHandler<PointT>::ModbusServerHandler(Logger logger)
    : logger_(logger)
    , netType_(modbus::ModbusServer::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

template<typename PointT>
ModbusServerHandler<PointT>::~ModbusServerHandler() {
    stop();
}

template<typename PointT>
void ModbusServerHandler<PointT>::initialize(const std::unordered_map<std::string, ValueType>& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it_port = config.find("port");
    if (it_port != config.end()) {
        if (auto pval = std::get_if<int>(&it_port->second)) {
            port_ = *pval;
        }
    }

    if (slaveIdList_.empty()) {
        slaveIdList_ = {1};
    }

    auto it_proto = config.find("protocol_type");
    if (it_proto != config.end()) {
        if (auto pval = std::get_if<int>(&it_proto->second)) {
            netType_ = (*pval == 0) ? modbus::ModbusServer::NetType::RTU
                                      : modbus::ModbusServer::NetType::TCP;
        }
    }

    if (netType_ == modbus::ModbusServer::NetType::RTU) {
        auto it_serial = config.find("serial_port");
        if (it_serial != config.end()) {
            if (auto pval = std::get_if<std::string>(&it_serial->second)) {
                connection_ = *pval;
            }
        } else {
            connection_ = "/dev/ttyUSB0";
        }

        int baudrate = 9600;
        auto it_baud = config.find("baudrate");
        if (it_baud != config.end()) {
            if (auto pval = std::get_if<int>(&it_baud->second)) {
                baudrate = *pval;
            }
        }

        std::string parity = "N";
        auto it_parity = config.find("parity");
        if (it_parity != config.end()) {
            if (auto pval = std::get_if<std::string>(&it_parity->second)) {
                parity = *pval;
            }
        }

        int stopbits = 1;
        auto it_stop = config.find("stopbits");
        if (it_stop != config.end()) {
            if (auto pval = std::get_if<int>(&it_stop->second)) {
                stopbits = *pval;
            }
        }

        settings_ = std::to_string(baudrate) + parity + std::to_string(stopbits);
    } else {
        connection_ = "*";
        settings_ = std::to_string(port_);
    }

    server_ = std::make_shared<modbus::ModbusServer>(netType_, connection_, settings_);

    for (int slaveId : slaveIdList_) {
        server_->addSlave(slaveId);
    }

    if (logger_) {
        std::ostringstream oss;
        oss << "Modbus 服务端初始化: port=" << port_
            << ", slave_id_list=[";
        for (size_t i = 0; i < slaveIdList_.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << slaveIdList_[i];
        }
        oss << "]";
        logger_(oss.str());
    }
}

template<typename PointT>
std::future<bool> ModbusServerHandler<PointT>::start() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!server_) {
            return false;
        }
        if (server_->isRunning()) {
            return true;
        }

        bool result = server_->start();
        serverStarted_ = result;
        this->running_ = result;
        return result;
    });
}

template<typename PointT>
std::future<bool> ModbusServerHandler<PointT>::stop() {
    return std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_ && server_->isRunning()) {
            server_->stop();
        }
        serverStarted_ = false;
        this->running_ = false;
        return true;
    });
}

template<typename PointT>
ValueType ModbusServerHandler<PointT>::readValue(const std::shared_ptr<PointT>& point) {
    if (!server_ || !point) {
        return 0;
    }

    int slave_id = 1;
    int address = 0;
    std::string decode = "0x41";

    if constexpr (requires { std::declval<PointT>().rtu_addr; }) {
        slave_id = point->rtu_addr;
    }
    if constexpr (requires { std::declval<PointT>().address; }) {
        if constexpr (std::is_same_v<decltype(point->address), std::string>) {
            address = parseAddress(point->address);
        } else {
            address = static_cast<int>(point->address);
        }
    }
    if constexpr (requires { std::declval<PointT>().decode; }) {
        decode = point->decode;
    }
    if constexpr (requires { std::declval<PointT>().func_code; }) {
        int func_code = point->func_code;
        return getValueByAddressWithDecode(func_code, slave_id, address, decode);
    }

    return 0;
}

template<typename PointT>
bool ModbusServerHandler<PointT>::writeValue(const std::shared_ptr<PointT>& point,
                                              const ValueType& value) {
    if (!server_ || !point) {
        return false;
    }

    int slave_id = 1;
    int address = 0;
    std::string decode = "0x41";
    int bit_offset = -1;
    int func_code = 3;

    if constexpr (requires { std::declval<PointT>().rtu_addr; }) {
        slave_id = point->rtu_addr;
    }
    if constexpr (requires { std::declval<PointT>().address; }) {
        if constexpr (std::is_same_v<decltype(point->address), std::string>) {
            address = parseAddress(point->address);
        } else {
            address = static_cast<int>(point->address);
        }
    }
    if constexpr (requires { std::declval<PointT>().decode; }) {
        decode = point->decode;
    }
    if constexpr (requires { std::declval<PointT>().bit; }) {
        bit_offset = point->bit;
    }
    if constexpr (requires { std::declval<PointT>().func_code; }) {
        func_code = point->func_code;
    }

    ValueType write_val = value;
    if (bit_offset >= 0) {
        ValueType cur_val = getValueByAddressWithDecode(func_code, slave_id, address, decode);
        if (auto cur_int = std::get_if<int>(&cur_val)) {
            int new_val = *cur_int;
            int val_int = 0;
            if (valueToInt(value, val_int)) {
                if (val_int) {
                    new_val |= (1 << bit_offset);
                } else {
                    new_val &= ~(1 << bit_offset);
                }
                write_val = new_val;
            }
        }
    }

    setValueByAddressWithDecode(func_code, slave_id, address, write_val, decode);

    if (logger_) {
        std::ostringstream oss;
        oss << "写入测点成功: value=";
        int val_int = 0;
        if (valueToInt(write_val, val_int)) {
            oss << val_int;
        } else {
            oss << "?";
        }
        logger_(oss.str());
    }

    return true;
}

template<typename PointT>
void ModbusServerHandler<PointT>::addPoints(const std::vector<std::shared_ptr<PointT>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) {
            points_.push_back(p);
        }
    }
}

template<typename PointT>
ValueType ModbusServerHandler<PointT>::getValueByAddress(int funcCode, int slaveId,
                                                              int address) {
    return getValueByAddressWithDecode(funcCode, slaveId, address, "0x41");
}

template<typename PointT>
ValueType ModbusServerHandler<PointT>::getValueByAddressWithDecode(int funcCode, int slaveId, int address,
                                                              const std::string& decode) {
    if (!server_) {
        return 0;
    }

    uint16_t value = 0;
    modbus::ModbusServer::ErrorCode ret = modbus::ModbusServer::ErrorCode::Success;

    switch (funcCode) {
        case 1: {
            bool val;
            ret = server_->readCoil(slaveId, address, val);
            if (ret == modbus::ModbusServer::ErrorCode::Success) {
                return val ? 1 : 0;
            }
            break;
        }
        case 2: {
            bool val;
            ret = server_->readDiscreteInput(slaveId, address, val);
            if (ret == modbus::ModbusServer::ErrorCode::Success) {
                return val ? 1 : 0;
            }
            break;
        }
        case 3: {
            ret = server_->readHoldingRegister(slaveId, address, value);
            break;
        }
        case 4: {
            ret = server_->readInputRegister(slaveId, address, value);
            break;
        }
    }

    if (ret != modbus::ModbusServer::ErrorCode::Success) {
        return 0;
    }

    return static_cast<int>(value);
}

template<typename PointT>
void ModbusServerHandler<PointT>::setValueByAddress(int funcCode, int slaveId,
                                                         int address,
                                                         const ValueType& value) {
    setValueByAddressWithDecode(funcCode, slaveId, address, value, "0x41");
}

template<typename PointT>
void ModbusServerHandler<PointT>::setValueByAddressWithDecode(int funcCode, int slaveId,
                                                         int address,
                                                         const ValueType& value,
                                                         const std::string& decode) {
    if (!server_) {
        return;
    }

    modbus::ModbusServer::ErrorCode ret = modbus::ModbusServer::ErrorCode::Success;
    int valInt = 0;

    if (valueToInt(value, valInt)) {
        switch (funcCode) {
            case 5:
                ret = server_->writeCoil(slaveId, address, (valInt != 0));
                break;
            case 6:
                ret = server_->writeHoldingRegister(slaveId, address, static_cast<uint16_t>(valInt));
                break;
            case 15: {
                CoilBits coils(1);
                coils.set(0, (valInt != 0));
                ret = server_->writeCoils(slaveId, address, coils.data(), 1);
                break;
            }
            case 16: {
                std::vector<uint16_t> regs(1, static_cast<uint16_t>(valInt));
                ret = server_->writeHoldingRegisters(slaveId, address, regs.data(), 1);
                break;
            }
        }
    } else if (auto val_bool = std::get_if<bool>(&value)) {
        switch (funcCode) {
            case 5:
                ret = server_->writeCoil(slaveId, address, *val_bool);
                break;
            case 6:
                ret = server_->writeHoldingRegister(slaveId, address, (*val_bool ? 1 : 0));
                break;
            case 15: {
                CoilBits coils(1);
                coils.set(0, *val_bool);
                ret = server_->writeCoils(slaveId, address, coils.data(), 1);
                break;
            }
            case 16: {
                std::vector<uint16_t> regs(1, (*val_bool ? 1 : 0));
                ret = server_->writeHoldingRegisters(slaveId, address, regs.data(), 1);
                break;
            }
        }
    }
}

template<typename PointT>
std::vector<CapturedMessage> ModbusServerHandler<PointT>::getCapturedMessages(int limit) const {
    std::vector<CapturedMessage> empty;
    return empty;
}

template<typename PointT>
void ModbusServerHandler<PointT>::clearCapturedMessages() {
}

template<typename PointT>
AvgTimeStats ModbusServerHandler<PointT>::getAvgTime() const {
    return AvgTimeStats{};
}

template<typename PointT>
void ModbusServerHandler<PointT>::setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    writeCallback_ = callback;
}

template<typename PointT>
void ModbusServerHandler<PointT>::onModbusWriteInternal(int slaveId, int funcCode,
                                                            int address,
                                                            const std::vector<int>& values) {
    if (funcCode != 5 && funcCode != 6 && funcCode != 15 && funcCode != 16) {
        return;
    }

    int write_start_addr = address;
    int write_end_addr = address + static_cast<int>(values.size()) - 1;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& point : points_) {
        int point_slave_id = 1;
        int point_addr = 0;
        int pointFuncCode = 3;
        int register_cnt = 1;

        if constexpr (requires { std::declval<PointT>().rtu_addr; }) {
            point_slave_id = point->rtu_addr;
        }
        if constexpr (requires { std::declval<PointT>().address; }) {
            if constexpr (std::is_same_v<decltype(point->address), std::string>) {
                point_addr = parseAddress(point->address);
            } else {
                point_addr = static_cast<int>(point->address);
            }
        }
        if constexpr (requires { std::declval<PointT>().func_code; }) {
            pointFuncCode = point->func_code;
        }
        if constexpr (requires { std::declval<PointT>().register_cnt; }) {
            register_cnt = point->register_cnt;
        } else {
            std::string decode = "0x41";
            if constexpr (requires { std::declval<PointT>().decode; }) {
                decode = point->decode;
            }
            register_cnt = getRegisterCount(decode);
        }

        if (point_slave_id != slaveId) {
            continue;
        }

        bool isHoldingReg = (pointFuncCode == 3 || pointFuncCode == 6 || pointFuncCode == 16) &&
                              (funcCode == 6 || funcCode == 16);
        bool isCoil = (pointFuncCode == 1 || pointFuncCode == 5 || pointFuncCode == 15) &&
                       (funcCode == 5 || funcCode == 15);

        if (isHoldingReg || isCoil) {
            int point_end_addr = point_addr + register_cnt - 1;

            if (std::max(point_addr, write_start_addr) <= std::min(point_end_addr, write_end_addr)) {
                if (writeCallback_) {
                    writeCallback_->onModbusClientWrite(slaveId, funcCode, address, values);
                }
            }
        }
    }
}

template<typename PointT>
int ModbusServerHandler<PointT>::parseAddress(const std::string& addressStr) const {
    if (addressStr.empty()) {
        return 0;
    }
    if (addressStr.length() > 2 &&
        (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")) {
        return std::stoi(addressStr, nullptr, 16);
    }
    return std::stoi(addressStr);
}

template<typename PointT>
int ModbusServerHandler<PointT>::getRegisterCount(const std::string& decode) const {
    if (decode.empty()) return 1;
    if (decode == "0x41" || decode == "AB" || decode == "BA") return 1;
    if (decode == "0x42" || decode == "ABCD" || decode == "CDAB" ||
        decode == "BADC" || decode == "DCBA") return 2;
    if (decode == "0x43" || decode == "ABCDEFGH") return 4;
    return 1;
}

template<typename PointT>
bool ModbusServerHandler<PointT>::valueToInt(const ValueType& value, int& result) const {
    if (auto v = std::get_if<int>(&value)) {
        result = *v;
        return true;
    }
    if (auto v = std::get_if<int64_t>(&value)) {
        result = static_cast<int>(*v);
        return true;
    }
    if (auto v = std::get_if<double>(&value)) {
        result = static_cast<int>(std::round(*v));
        return true;
    }
    if (auto v = std::get_if<bool>(&value)) {
        result = *v ? 1 : 0;
        return true;
    }
    if (auto v = std::get_if<std::string>(&value)) {
        try {
            result = std::stoi(*v);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

template<typename PointT>
ModbusClientHandler<PointT>::ModbusClientHandler()
    : netType_(modbus::ModbusClient::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

template<typename PointT>
ModbusClientHandler<PointT>::ModbusClientHandler(Logger logger)
    : logger_(logger)
    , netType_(modbus::ModbusClient::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

template<typename PointT>
ModbusClientHandler<PointT>::~ModbusClientHandler() {
    disconnect();
}

template<typename PointT>
void ModbusClientHandler<PointT>::initialize(const std::unordered_map<std::string, ValueType>& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it_ip = config.find("ip");
    if (it_ip != config.end()) {
        if (auto pval = std::get_if<std::string>(&it_ip->second)) {
            connection_ = *pval;
        }
    } else {
        auto it_host = config.find("host");
        if (it_host != config.end()) {
            if (auto pval = std::get_if<std::string>(&it_host->second)) {
                connection_ = *pval;
            }
        }
    }

    int port = 502;
    auto it_port = config.find("port");
    if (it_port != config.end()) {
        if (auto pval = std::get_if<int>(&it_port->second)) {
            port = *pval;
        }
    }
    settings_ = std::to_string(port);

    auto it_proto = config.find("protocol_type");
    if (it_proto != config.end()) {
        if (auto pval = std::get_if<int>(&it_proto->second)) {
            netType_ = (*pval == 0) ? modbus::ModbusClient::NetType::RTU
                                      : modbus::ModbusClient::NetType::TCP;
        }
    }

    if (netType_ == modbus::ModbusClient::NetType::RTU) {
        auto it_serial = config.find("serial_port");
        if (it_serial != config.end()) {
            if (auto pval = std::get_if<std::string>(&it_serial->second)) {
                connection_ = *pval;
            }
        } else {
            connection_ = "/dev/ttyUSB0";
        }

        int baudrate = 9600;
        auto it_baud = config.find("baudrate");
        if (it_baud != config.end()) {
            if (auto pval = std::get_if<int>(&it_baud->second)) {
                baudrate = *pval;
            }
        }

        std::string parity = "N";
        auto it_parity = config.find("parity");
        if (it_parity != config.end()) {
            if (auto pval = std::get_if<std::string>(&it_parity->second)) {
                parity = *pval;
            }
        }

        int stopbits = 1;
        auto it_stop = config.find("stopbits");
        if (it_stop != config.end()) {
            if (auto pval = std::get_if<int>(&it_stop->second)) {
                stopbits = *pval;
            }
        }

        settings_ = std::to_string(baudrate) + parity + std::to_string(stopbits);
    }

    auto itSlave = config.find("slave_id");
    if (itSlave != config.end()) {
        if (auto pval = std::get_if<int>(&itSlave->second)) {
            defaultSlaveId_ = *pval;
        }
    }

    auto itTimeout = config.find("response_timeout");
    if (itTimeout != config.end()) {
        if (auto pval = std::get_if<int>(&itTimeout->second)) {
            responseTimeoutMs_ = *pval;
        }
    }

    client_ = std::make_shared<modbus::ModbusClient>(netType_, connection_, settings_);
    client_->setResponseTimeout(responseTimeoutMs_);
    client_->addSlave(defaultSlaveId_);
    clientInitialized_ = true;

    if (logger_) {
        std::ostringstream oss;
        oss << "Modbus 客户端初始化: ip=" << connection_
            << ", port=" << port
            << ", slave_id=" << defaultSlaveId_;
        logger_(oss.str());
    }
}

template<typename PointT>
std::future<bool> ModbusClientHandler<PointT>::start() {
    return std::async(std::launch::async, [this]() {
        return connect();
    });
}

template<typename PointT>
std::future<bool> ModbusClientHandler<PointT>::stop() {
    return std::async(std::launch::async, [this]() {
        disconnect();
        return true;
    });
}

template<typename PointT>
bool ModbusClientHandler<PointT>::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) {
        return false;
    }

    try {
        bool result = client_->connect();
        connected_ = result;
        this->running_ = result;
        return result;
    } catch (const std::exception& e) {
        if (logger_) {
            std::ostringstream oss;
            oss << "连接 Modbus 服务器失败: " << e.what();
            logger_(oss.str());
        }
        return false;
    }
}

template<typename PointT>
void ModbusClientHandler<PointT>::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_) {
        client_->disconnect();
    }
    connected_ = false;
    this->running_ = false;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::isConnected() const {
    if (!connected_) {
        return false;
    }
    if (!client_) {
        return false;
    }
    return client_->isConnected();
}

template<typename PointT>
ValueType ModbusClientHandler<PointT>::readValue(const std::shared_ptr<PointT>& point) {
    if (!client_ || !isConnected() || !point) {
        return {};
    }

    int slaveId = defaultSlaveId_;
    int address = 0;
    std::string decode = "0x41";
    int funcCode = 3;

    if constexpr (requires { std::declval<PointT>().rtu_addr; }) {
        slaveId = point->rtu_addr;
    }
    if constexpr (requires { std::declval<PointT>().address; }) {
        if constexpr (std::is_same_v<decltype(point->address), std::string>) {
            address = parseAddress(point->address);
        } else {
            address = static_cast<int>(point->address);
        }
    }
    if constexpr (requires { std::declval<PointT>().decode; }) {
        decode = point->decode;
    }
    if constexpr (requires { std::declval<PointT>().func_code; }) {
        funcCode = point->func_code;
    }

    return readValueInternal(funcCode, slaveId, address, decode);
}

template<typename PointT>
ValueType ModbusClientHandler<PointT>::readValueInternal(int funcCode, int slaveId,
                                                             int address,
                                                             const std::string& decode) {
    modbus::ModbusClient::ErrorCode ret = modbus::ModbusClient::ErrorCode::Success;

    switch (funcCode) {
        case 1: {
            bool val;
            ret = client_->readCoils(slaveId, address, &val, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) {
                return val ? 1 : 0;
            }
            break;
        }
        case 2: {
            bool val;
            ret = client_->readDiscreteInputs(slaveId, address, &val, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) {
                return val ? 1 : 0;
            }
            break;
        }
        case 3: {
            uint16_t val;
            ret = client_->readHoldingRegisters(slaveId, address, &val, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) {
                return static_cast<int>(val);
            }
            break;
        }
        case 4: {
            uint16_t val;
            ret = client_->readInputRegisters(slaveId, address, &val, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) {
                return static_cast<int>(val);
            }
            break;
        }
    }

    return {};
}

template<typename PointT>
bool ModbusClientHandler<PointT>::writeValue(const std::shared_ptr<PointT>& point,
                                               const ValueType& value) {
    if (!client_ || !isConnected() || !point) {
        return false;
    }

    int slaveId = defaultSlaveId_;
    int address = 0;
    int funcCode = 6;

    if constexpr (requires { std::declval<PointT>().rtu_addr; }) {
        slaveId = point->rtu_addr;
    }
    if constexpr (requires { std::declval<PointT>().address; }) {
        if constexpr (std::is_same_v<decltype(point->address), std::string>) {
            address = parseAddress(point->address);
        } else {
            address = static_cast<int>(point->address);
        }
    }
    if constexpr (requires { std::declval<PointT>().func_code; }) {
        funcCode = point->func_code;
    }

    modbus::ModbusClient::ErrorCode ret = modbus::ModbusClient::ErrorCode::Success;
    int valInt = 0;

    if (valueToInt(value, valInt)) {
        switch (funcCode) {
            case 5:
                ret = client_->writeSingleCoil(slaveId, address, (valInt != 0));
                break;
            case 6:
                ret = client_->writeSingleRegister(slaveId, address, static_cast<uint16_t>(valInt));
                break;
            case 15: {
                CoilBits coils(1);
                coils.set(0, (valInt != 0));
                ret = client_->writeMultipleCoils(slaveId, address, coils.data(), 1);
                break;
            }
            case 16: {
                std::vector<uint16_t> regs(1, static_cast<uint16_t>(valInt));
                ret = client_->writeMultipleRegisters(slaveId, address, regs.data(), 1);
                break;
            }
            default:
                ret = client_->writeSingleRegister(slaveId, address, static_cast<uint16_t>(valInt));
                break;
        }
    } else if (auto val_bool = std::get_if<bool>(&value)) {
        switch (funcCode) {
            case 5:
            case 1:
                ret = client_->writeSingleCoil(slaveId, address, *val_bool);
                break;
            default:
                ret = client_->writeSingleRegister(slaveId, address, (*val_bool ? 1 : 0));
                break;
        }
    }

    return ret == modbus::ModbusClient::ErrorCode::Success;
}

template<typename PointT>
void ModbusClientHandler<PointT>::addPoints(const std::vector<std::shared_ptr<PointT>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) {
            points_.push_back(p);
        }
    }
}

template<typename PointT>
std::vector<CapturedMessage> ModbusClientHandler<PointT>::getCapturedMessages(int limit) const {
    std::vector<CapturedMessage> empty;
    return empty;
}

template<typename PointT>
void ModbusClientHandler<PointT>::clearCapturedMessages() {
}

template<typename PointT>
AvgTimeStats ModbusClientHandler<PointT>::getAvgTime() const {
    return AvgTimeStats{};
}

template<typename PointT>
std::vector<int> ModbusClientHandler<PointT>::readRegistersBatchAsync(
    int func_code, int slave_id, int start_address, int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;

    if (!client_ || !isConnected()) {
        return result;
    }

    modbus::ModbusClient::ErrorCode ret = modbus::ModbusClient::ErrorCode::Success;
    std::vector<uint16_t> values(count);

    switch (func_code) {
        case 3:
        case 6:
        case 16:
            ret = client_->readHoldingRegisters(slave_id, start_address, values.data(), count);
            break;
        case 4:
            ret = client_->readInputRegisters(slave_id, start_address, values.data(), count);
            break;
        default:
            return result;
    }

    if (ret == modbus::ModbusClient::ErrorCode::Success) {
        result.reserve(count);
        for (int i = 0; i < count; ++i) {
            result.push_back(static_cast<int>(values[i]));
        }
    }

    return result;
}

template<typename PointT>
std::vector<int> ModbusClientHandler<PointT>::readHoldingRegisters(
    int slave_id, int start_address, int count) {
    return readRegistersBatchAsync(3, slave_id, start_address, count);
}

template<typename PointT>
std::vector<int> ModbusClientHandler<PointT>::readInputRegisters(
    int slave_id, int start_address, int count) {
    return readRegistersBatchAsync(4, slave_id, start_address, count);
}

template<typename PointT>
CoilBits ModbusClientHandler<PointT>::readCoils(
    int slave_id, int start_address, int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    CoilBits result;

    if (!client_ || !isConnected()) {
        return result;
    }

    CoilBits values(count);
    auto ret = client_->readCoils(slave_id, start_address, values.data(), count);

    if (ret == modbus::ModbusClient::ErrorCode::Success) {
        result = std::move(values);
    }

    return result;
}

template<typename PointT>
CoilBits ModbusClientHandler<PointT>::readDiscreteInputs(
    int slave_id, int start_address, int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    CoilBits result;

    if (!client_ || !isConnected()) {
        return result;
    }

    CoilBits values(count);
    auto ret = client_->readDiscreteInputs(slave_id, start_address, values.data(), count);

    if (ret == modbus::ModbusClient::ErrorCode::Success) {
        result = std::move(values);
    }

    return result;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::writeSingleRegister(
    int slave_id, int address, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) {
        return false;
    }
    auto ret = client_->writeSingleRegister(slave_id, address, static_cast<uint16_t>(value));
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::writeMultipleRegisters(
    int slave_id, int start_address, const std::vector<int>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected() || values.empty()) {
        return false;
    }

    std::vector<uint16_t> regs(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        regs[i] = static_cast<uint16_t>(values[i]);
    }

    auto ret = client_->writeMultipleRegisters(slave_id, start_address, regs.data(),
                                                static_cast<int>(regs.size()));
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::writeSingleCoil(
    int slave_id, int address, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) {
        return false;
    }
    auto ret = client_->writeSingleCoil(slave_id, address, value);
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::writeMultipleCoils(
    int slave_id, int start_address, const CoilBits& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected() || values.size() == 0) {
        return false;
    }
    auto ret = client_->writeMultipleCoils(slave_id, start_address, values.data(),
                                             static_cast<int>(values.size()));
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

template<typename PointT>
int ModbusClientHandler<PointT>::extractBitValue(int value, int bit_offset) const {
    return (value >> bit_offset) & 1;
}

template<typename PointT>
int ModbusClientHandler<PointT>::applyBitValue(int current_value, int bit_offset,
                                                   bool bit_value) const {
    if (bit_value) {
        return current_value | (1 << bit_offset);
    } else {
        return current_value & ~(1 << bit_offset);
    }
}

template<typename PointT>
int ModbusClientHandler<PointT>::parseAddress(const std::string& address_str) const {
    if (address_str.empty()) {
        return 0;
    }
    if (address_str.length() > 2 &&
        (address_str.substr(0, 2) == "0x" || address_str.substr(0, 2) == "0X")) {
        return std::stoi(address_str, nullptr, 16);
    }
    return std::stoi(address_str);
}

template<typename PointT>
int ModbusClientHandler<PointT>::getRegisterCount(const std::string& decode) const {
    if (decode.empty()) return 1;
    if (decode == "0x41" || decode == "AB" || decode == "BA") return 1;
    if (decode == "0x42" || decode == "ABCD" || decode == "CDAB" ||
        decode == "BADC" || decode == "DCBA") return 2;
    if (decode == "0x43" || decode == "ABCDEFGH") return 4;
    return 1;
}

template<typename PointT>
bool ModbusClientHandler<PointT>::valueToInt(const ValueType& value, int& result) const {
    if (auto v = std::get_if<int>(&value)) {
        result = *v;
        return true;
    }
    if (auto v = std::get_if<int64_t>(&value)) {
        result = static_cast<int>(*v);
        return true;
    }
    if (auto v = std::get_if<double>(&value)) {
        result = static_cast<int>(std::round(*v));
        return true;
    }
    if (auto v = std::get_if<bool>(&value)) {
        result = *v ? 1 : 0;
        return true;
    }
    if (auto v = std::get_if<std::string>(&value)) {
        try {
            result = std::stoi(*v);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

} // namespace device
} // namespace net
} // namespace ems

#endif // EMS_NET_DEVICE_PROTOCOL_HANDLER_MODBUS_H
