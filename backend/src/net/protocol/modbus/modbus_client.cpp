/**
 * @file modbus_client.cpp
 * @brief Modbus 客户端实现
 */
#include "modbus_client.h"
#include <modbus.h>
#include <cerrno>
#include <cstring>

namespace ems {
namespace net {
namespace modbus {

ModbusClient::ModbusClient(NetType type, const std::string& connection, const std::string& settings)
    : net_type_(type), connection_(connection), settings_(settings) {
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

    try {
        if (!master_.open()) {
            last_error_msg_ = "Failed to open Modbus connection";
            return false;
        }

        connected_ = true;
        return true;
    }
    catch (const std::exception& e) {
        last_error_msg_ = e.what();
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

Modbus::Slave* ModbusClient::getSlave(int slave_id) {
    auto it = slaves_.find(slave_id);
    if (it != slaves_.end()) {
        return it->second.get();
    }
    if (!isConnected()) {
        return nullptr;
    }
    auto& slave = slaves_[slave_id];
    slave = std::make_unique<Modbus::Slave>(slave_id, &master_);
    return slave.get();
}

bool ModbusClient::addSlave(int slave_id) {
    if (!isConnected()) {
        return false;
    }
    if (slaves_.find(slave_id) != slaves_.end()) {
        return false;
    }
    slaves_[slave_id] = std::make_unique<Modbus::Slave>(slave_id, &master_);
    return true;
}

bool ModbusClient::removeSlave(int slave_id) {
    auto it = slaves_.find(slave_id);
    if (it == slaves_.end()) {
        return false;
    }
    slaves_.erase(it);
    return true;
}

bool ModbusClient::hasSlave(int slave_id) const {
    return slaves_.find(slave_id) != slaves_.end();
}

ModbusClient::ErrorCode ModbusClient::translateError(int modbus_ret) {
    if (modbus_ret >= 0) {
        return ModbusClient::ErrorCode::Success;
    }

    switch (errno) {
        case EBADMSG:
        case EINVAL:
        case EPROTO:
            last_error_ = errno;
            last_error_msg_ = std::strerror(errno);
            return ErrorCode::SlaveError;

        case EBUSY:
            last_error_ = errno;
            last_error_msg_ = "Slave busy";
            return ErrorCode::SlaveError;

        case ETIMEDOUT:
        case ECOMM:
            last_error_ = errno;
            last_error_msg_ = "Communication timeout";
            return ErrorCode::Timeout;

        default:
            last_error_ = errno;
            last_error_msg_ = std::strerror(errno);
            return ErrorCode::Unknown;
    }
}

ModbusClient::ErrorCode ModbusClient::readCoils(int slave_id, int addr, bool* dest, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readCoils(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readDiscreteInputs(int slave_id, int addr, bool* dest, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readDiscreteInputs(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readHoldingRegisters(int slave_id, int addr, uint16_t* dest, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readRegisters(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::readInputRegisters(int slave_id, int addr, uint16_t* dest, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->readInputRegisters(addr, dest, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeSingleCoil(int slave_id, int addr, bool value) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeCoil(addr, value);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeSingleRegister(int slave_id, int addr, uint16_t value) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeRegister(addr, value);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeMultipleCoils(int slave_id, int addr, const bool* src, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeCoils(addr, src, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeMultipleRegisters(int slave_id, int addr, const uint16_t* src, int nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeRegisters(addr, src, nb);
    return translateError(ret);
}

ModbusClient::ErrorCode ModbusClient::writeReadRegisters(int slave_id, int write_addr, const uint16_t* src, int write_nb,
                                           int read_addr, uint16_t* dest, int read_nb) {
    auto* slave = getSlave(slave_id);
    if (!slave) {
        return ErrorCode::ConnectionFailed;
    }
    int ret = slave->writeReadRegisters(write_addr, src, write_nb, read_addr, dest, read_nb);
    return translateError(ret);
}

} // namespace modbus
} // namespace net
} // namespace ems
