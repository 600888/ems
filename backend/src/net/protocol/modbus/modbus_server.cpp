/**
 * @file modbus_server.cpp
 * @brief Modbus 服务端实现
 */
#include "modbus_server.h"

#include <spdlog/fmt/bundled/format.h>

#include <algorithm>
#include <iostream>

#include "net/log.h"
#include "net/protocol/modbus/modbus_limits.h"

namespace ems {
namespace net {
namespace modbus {

ModbusServer::ModbusServer(NetType type, const std::string& connection, const std::string& settings)
    : netType_(type), connection_(connection), settings_(settings) {
    Modbus::Net net = (type == NetType::RTU) ? Modbus::Net::Rtu : Modbus::Net::Tcp;
    server_.setBackend(net, connection_, settings_);
}

ModbusServer::~ModbusServer() { stop(); }

bool ModbusServer::start() {
    if (running_.load()) {
        return false;
    }

    try {
        if (!server_.open()) {
            LOG_ERROR(
                fmt::format("ModbusServer[{}:{}]: failed to open", connection_, settings_).c_str());
            return false;
        }

        running_ = true;
        serverThread_ = std::make_unique<std::thread>(&ModbusServer::runLoop, this);

        LOG_INFO(fmt::format("ModbusServer[{}:{}] 启动成功", connection_, settings_).c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer[{}:{}] 启动异常: {}", connection_, settings_, e.what())
                      .c_str());
        return false;
    }
}

void ModbusServer::stop() {
    if (!running_.load()) {
        return;
    }

    running_ = false;

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }

    server_.close();
}

bool ModbusServer::isRunning() const { return running_.load(); }

void ModbusServer::join() {
    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }
}

int ModbusServer::addSlave(int slaveId) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (slaves_.find(slaveId) != slaves_.end()) {
        return -1;
    }

    try {
        auto& slave = server_.addSlave(slaveId);
        slaves_[slaveId] =
            std::shared_ptr<Modbus::BufferedSlave>(&slave, [](Modbus::BufferedSlave*) {});
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::addSlave[{}] 异常: {}", slaveId, e.what()).c_str());
        return -1;
    }
}

bool ModbusServer::removeSlave(int slaveId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }

    slaves_.erase(it);
    return true;
}

bool ModbusServer::hasSlave(int slaveId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slaves_.find(slaveId) != slaves_.end();
}

bool ModbusServer::setHoldingRegisterBlock(int slaveId, int startAddr, int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }

    try {
        it->second->setBlock(Modbus::Table::HoldingRegister, count, startAddr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::setHoldingRegisterBlock[slave={}]: {}", slaveId, e.what())
                .c_str());
        return false;
    }
}

bool ModbusServer::setInputRegisterBlock(int slaveId, int startAddr, int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }

    try {
        it->second->setBlock(Modbus::Table::InputRegister, count, startAddr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::setInputRegisterBlock[slave={}]: {}", slaveId, e.what())
                .c_str());
        return false;
    }
}

bool ModbusServer::setCoilBlock(int slaveId, int startAddr, int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }

    try {
        it->second->setBlock(Modbus::Table::Coil, count, startAddr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::setCoilBlock[slave={}]: {}", slaveId, e.what()).c_str());
        return false;
    }
}

bool ModbusServer::setDiscreteInputBlock(int slaveId, int startAddr, int count) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return false;
    }

    try {
        it->second->setBlock(Modbus::Table::DiscreteInput, count, startAddr);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::setDiscreteInputBlock[slave={}]: {}", slaveId, e.what())
                .c_str());
        return false;
    }
}

ModbusServer::ErrorCode ModbusServer::writeHoldingRegister(int slaveId, int addr, uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeRegister(addr, value);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::HoldingRegister, addr, 1);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::writeHoldingRegister[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::writeHoldingRegisters(int slaveId, int addr,
                                                            const uint16_t* src, int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeRegisters(addr, src, nb);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::HoldingRegister, addr, nb);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::writeHoldingRegisters[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::writeInputRegister(int slaveId, int addr, uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeInputRegister(addr, value);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::InputRegister, addr, 1);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::writeInputRegister[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::writeInputRegisters(int slaveId, int addr,
                                                          const uint16_t* src, int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeInputRegisters(addr, src, nb);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::InputRegister, addr, nb);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::writeInputRegisters[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::writeCoil(int slaveId, int addr, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeCoil(addr, value);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::Coil, addr, 1);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::writeCoil[slave={},addr={}]: {}", slaveId, addr, e.what())
                .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::writeCoils(int slaveId, int addr, const bool* src, int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->writeCoils(addr, src, nb);
        if (dataChangeCallback_) {
            dataChangeCallback_(slaveId, Modbus::Table::Coil, addr, nb);
        }
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::writeCoils[slave={},addr={}]: {}", slaveId, addr, e.what())
                .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readHoldingRegister(int slaveId, int addr, uint16_t& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readRegister(addr, value);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readHoldingRegister[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readHoldingRegisters(int slaveId, int addr, uint16_t* dest,
                                                           int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readRegisters(addr, dest, nb);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readHoldingRegisters[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readInputRegister(int slaveId, int addr, uint16_t& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readInputRegister(addr, value);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readInputRegister[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readInputRegisters(int slaveId, int addr, uint16_t* dest,
                                                         int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readInputRegisters(addr, dest, nb);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readInputRegisters[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readCoil(int slaveId, int addr, bool& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readCoils(addr, &value, 1);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::readCoil[slave={},addr={}]: {}", slaveId, addr, e.what())
                .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readCoils(int slaveId, int addr, bool* dest, int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readCoils(addr, dest, nb);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(
            fmt::format("ModbusServer::readCoils[slave={},addr={}]: {}", slaveId, addr, e.what())
                .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readDiscreteInput(int slaveId, int addr, bool& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readDiscreteInputs(addr, &value, 1);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readDiscreteInput[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

ModbusServer::ErrorCode ModbusServer::readDiscreteInputs(int slaveId, int addr, bool* dest,
                                                         int nb) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = slaves_.find(slaveId);
    if (it == slaves_.end()) {
        return ErrorCode::Unknown;
    }

    try {
        it->second->readDiscreteInputs(addr, dest, nb);
        return ErrorCode::Success;
    } catch (const std::exception& e) {
        LOG_ERROR(fmt::format("ModbusServer::readDiscreteInputs[slave={},addr={}]: {}", slaveId,
                              addr, e.what())
                      .c_str());
        return ErrorCode::Unknown;
    }
}

void ModbusServer::runLoop() {
    while (running_.load()) {
        server_.poll(enum_value(Defaults::ServerPollMs));
    }
}

}  // namespace modbus
}  // namespace net
}  // namespace ems
