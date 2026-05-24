/**
 * @file modbus_client.cpp
 * @brief Modbus 客户端实现
 */
#include "modbus_client.h"
#include "net/log.h"
#include <modbus.h>
#include <cerrno>
#include <cstring>
#include <spdlog/fmt/bundled/format.h>

namespace ems {
namespace net {
namespace modbus {

ModbusClient::ModbusClient(NetType type, const std::string& connection, const std::string& settings)
    : netType_(type), connection_(connection), settings_(settings) {
    Modbus::Net net = (type == NetType::RTU) ? Modbus::Net::Rtu : Modbus::Net::Tcp;
    master_.setBackend(net, connection_, settings_);
}

ModbusClient::~ModbusClient() {
    disconnect();
}

bool ModbusClient::connect() {
    if (connected_) {
        disconnect();
    }

    LOG_INFO(fmt::format("Modbus客户端连接: type={}, connection={}, settings={}",
                         static_cast<int>(netType_), connection_, settings_).c_str());
    try {
        if (!master_.open()) {
            lastErrorMsg_ = "Failed to open Modbus connection";
            LOG_ERROR(fmt::format("Modbus客户端打开连接失败: {}", lastErrorMsg_).c_str());
            return false;
        }

        connected_ = true;
        LOG_INFO(fmt::format("Modbus客户端连接成功: type={}, connection={}",
                             static_cast<int>(netType_), connection_).c_str());
        return true;
    }
    catch (const std::exception& e) {
        lastErrorMsg_ = e.what();
        LOG_ERROR(fmt::format("Modbus客户端连接异常: {}", lastErrorMsg_).c_str());
        return false;
    }
}

void ModbusClient::disconnect() {
    if (connected_) {
        master_.close();
        slaves_.clear();
        connected_ = false;
    }
}

bool ModbusClient::isConnected() const {
    return connected_ && master_.isOpen();
}

void ModbusClient::setResponseTimeout(int ms) {
    master_.setResponseTimeout(ms / 1000.0);
}

void ModbusClient::setByteTimeout(int ms) {
    master_.setByteTimeout(ms / 1000.0);
}

void ModbusClient::setDebug(bool enable) {
    master_.setDebug(enable);
}

Modbus::Slave* ModbusClient::getSlave(int slaveId) {
    auto it = slaves_.find(slaveId);
    if (it != slaves_.end()) {
        return it->second.get();
    }
    if (!isConnected()) {
        return nullptr;
    }
    auto& slave = slaves_[slaveId];
    slave = std::make_unique<Modbus::Slave>(slaveId, &master_);
    return slave.get();
}

bool ModbusClient::addSlave(int slaveId) {
    if (!isConnected()) {
        return false;
    }
    if (slaves_.find(slaveId) != slaves_.end()) {
        return false;
    }
    slaves_[slaveId] = std::make_unique<Modbus::Slave>(slaveId, &master_);
    return true;
}

bool ModbusClient::removeSlave(int slaveId) {
    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }
    slaves_.erase(it);
    return true;
}

bool ModbusClient::hasSlave(int slaveId) const {
    return slaves_.find(slaveId) != slaves_.end();
}

ModbusClient::ErrorCode ModbusClient::translateError(int modbusRet) {
    if (modbusRet >= 0) {
        return ModbusClient::ErrorCode::Success;
    }

    switch (errno) {
        case EBADMSG:
        case EINVAL:
        case EPROTO:
            lastError_ = errno;
            lastErrorMsg_ = std::strerror(errno);
            return ErrorCode::SlaveError;

        case EBUSY:
            lastError_ = errno;
            lastErrorMsg_ = "Slave busy";
            return ErrorCode::SlaveError;

        case ETIMEDOUT:
        case ECOMM:
            lastError_ = errno;
            lastErrorMsg_ = "Communication timeout";
            return ErrorCode::Timeout;

        default:
            lastError_ = errno;
            lastErrorMsg_ = std::strerror(errno);
            return ErrorCode::Unknown;
    }
}

ModbusClient::ErrorCode ModbusClient::readCoils(int slaveId, int addr, bool* dest, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readCoils(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readDiscreteInputs(int slaveId, int addr, bool* dest, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readDiscreteInputs(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readHoldingRegisters(int slaveId, int addr, uint16_t* dest, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readRegisters(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readInputRegisters(int slaveId, int addr, uint16_t* dest, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readInputRegisters(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeSingleCoil(int slaveId, int addr, bool value) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeCoil(addr, value);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeSingleRegister(int slaveId, int addr, uint16_t value) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeRegister(addr, value);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeMultipleCoils(int slaveId, int addr, const bool* src, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeCoils(addr, src, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeMultipleRegisters(int slaveId, int addr, const uint16_t* src, int nb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeRegisters(addr, src, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeReadRegisters(int slaveId, int writeAddr, const uint16_t* src, int writeNb,
                                           int readAddr, uint16_t* dest, int readNb) {
    auto* slave = getSlave(slaveId);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeReadRegisters(writeAddr, src, writeNb, readAddr, dest, readNb);
    return translateError(ret);
}

} // namespace modbus
} // namespace net
} // namespace ems
