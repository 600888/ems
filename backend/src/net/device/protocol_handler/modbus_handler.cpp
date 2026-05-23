/**
 * @file modbus_handler.cpp
 * @brief Modbus 协议处理器实现
 * @author cdy
 * @date 2026-05-22
 */

#include "modbus_handler.h"

namespace ems {
namespace net {
namespace device {

// ============================================================
// ModbusServerHandler
// ============================================================

ModbusServerHandler::ModbusServerHandler()
    : netType_(modbus::ModbusServer::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

ModbusServerHandler::ModbusServerHandler(Logger logger)
    : logger_(logger)
    , netType_(modbus::ModbusServer::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

ModbusServerHandler::~ModbusServerHandler() {
    stop();
}

void ModbusServerHandler::initialize(const std::unordered_map<std::string, ValueType>& config) {
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

std::future<bool> ModbusServerHandler::start() {
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

std::future<bool> ModbusServerHandler::stop() {
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

ValueType ModbusServerHandler::readValue(const std::shared_ptr<ProtocolPoint>& point) {
    if (!server_ || !point) {
        return 0;
    }

    return getValueByAddressWithDecode(
        point->func_code,
        point->rtu_addr,
        point->address,
        point->decode
    );
}

bool ModbusServerHandler::writeValue(const std::shared_ptr<ProtocolPoint>& point,
                                     const ValueType& value) {
    if (!server_ || !point) {
        return false;
    }

    int slave_id  = point->rtu_addr;
    int address   = point->address;
    std::string decode = point->decode;
    int bit_offset = point->bit;
    int func_code = point->func_code;

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

void ModbusServerHandler::addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) {
            points_.push_back(p);
        }
    }
}

ValueType ModbusServerHandler::getValueByAddress(int funcCode, int slaveId, int address) {
    return getValueByAddressWithDecode(funcCode, slaveId, address, "0x41");
}

ValueType ModbusServerHandler::getValueByAddressWithDecode(int funcCode, int slaveId,
                                                           int address,
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

void ModbusServerHandler::setValueByAddress(int funcCode, int slaveId, int address,
                                            const ValueType& value) {
    setValueByAddressWithDecode(funcCode, slaveId, address, value, "0x41");
}

void ModbusServerHandler::setValueByAddressWithDecode(int funcCode, int slaveId,
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

std::vector<CapturedMessage> ModbusServerHandler::getCapturedMessages(int limit) const {
    (void)limit;
    return {};
}

void ModbusServerHandler::clearCapturedMessages() {
}

AvgTimeStats ModbusServerHandler::getAvgTime() const {
    return AvgTimeStats{};
}

void ModbusServerHandler::setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    writeCallback_ = callback;
}

void ModbusServerHandler::onModbusWriteInternal(int slaveId, int funcCode,
                                                int address,
                                                const std::vector<int>& values) {
    if (funcCode != 5 && funcCode != 6 && funcCode != 15 && funcCode != 16) {
        return;
    }

    int write_start_addr = address;
    int write_end_addr = address + static_cast<int>(values.size()) - 1;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& point : points_) {
        int point_slave_id = point->rtu_addr;
        int point_addr = point->address;
        int pointFuncCode = point->func_code;
        int register_cnt = point->register_cnt;

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

int ModbusServerHandler::parseAddress(const std::string& addressStr) const {
    if (addressStr.empty()) {
        return 0;
    }
    if (addressStr.length() > 2 &&
        (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")) {
        return std::stoi(addressStr, nullptr, 16);
    }
    return std::stoi(addressStr);
}

int ModbusServerHandler::getRegisterCount(const std::string& decode) const {
    if (decode.empty()) return 1;
    if (decode == "0x41" || decode == "AB" || decode == "BA") return 1;
    if (decode == "0x42" || decode == "ABCD" || decode == "CDAB" ||
        decode == "BADC" || decode == "DCBA") return 2;
    if (decode == "0x43" || decode == "ABCDEFGH") return 4;
    return 1;
}

bool ModbusServerHandler::valueToInt(const ValueType& value, int& result) const {
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

// ============================================================
// ModbusClientHandler
// ============================================================

ModbusClientHandler::ModbusClientHandler()
    : netType_(modbus::ModbusClient::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

ModbusClientHandler::ModbusClientHandler(Logger logger)
    : logger_(logger)
    , netType_(modbus::ModbusClient::NetType::TCP)
    , connection_("127.0.0.1")
    , settings_("502") {
}

ModbusClientHandler::~ModbusClientHandler() {
    disconnect();
}

void ModbusClientHandler::initialize(const std::unordered_map<std::string, ValueType>& config) {
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

std::future<bool> ModbusClientHandler::start() {
    return std::async(std::launch::async, [this]() {
        return connect();
    });
}

std::future<bool> ModbusClientHandler::stop() {
    return std::async(std::launch::async, [this]() {
        disconnect();
        return true;
    });
}

bool ModbusClientHandler::connect() {
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

void ModbusClientHandler::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_) {
        client_->disconnect();
    }
    connected_ = false;
    this->running_ = false;
}

bool ModbusClientHandler::isConnected() const {
    if (!connected_) {
        return false;
    }
    if (!client_) {
        return false;
    }
    return client_->isConnected();
}

ValueType ModbusClientHandler::readValue(const std::shared_ptr<ProtocolPoint>& point) {
    if (!client_ || !isConnected() || !point) {
        return {};
    }

    return readValueInternal(
        point->func_code,
        point->rtu_addr,
        point->address,
        point->decode
    );
}

ValueType ModbusClientHandler::readValueInternal(int funcCode, int slaveId,
                                                  int address,
                                                  const std::string& decode) {
    (void)decode;
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

bool ModbusClientHandler::writeValue(const std::shared_ptr<ProtocolPoint>& point,
                                     const ValueType& value) {
    if (!client_ || !isConnected() || !point) {
        return false;
    }

    int slaveId  = point->rtu_addr;
    int address  = point->address;
    int funcCode = point->func_code;

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

void ModbusClientHandler::addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) {
            points_.push_back(p);
        }
    }
}

std::vector<CapturedMessage> ModbusClientHandler::getCapturedMessages(int limit) const {
    (void)limit;
    return {};
}

void ModbusClientHandler::clearCapturedMessages() {
}

AvgTimeStats ModbusClientHandler::getAvgTime() const {
    return AvgTimeStats{};
}

std::vector<int> ModbusClientHandler::readRegistersBatchAsync(
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

std::vector<int> ModbusClientHandler::readHoldingRegisters(
    int slave_id, int start_address, int count) {
    return readRegistersBatchAsync(3, slave_id, start_address, count);
}

std::vector<int> ModbusClientHandler::readInputRegisters(
    int slave_id, int start_address, int count) {
    return readRegistersBatchAsync(4, slave_id, start_address, count);
}

CoilBits ModbusClientHandler::readCoils(int slave_id, int start_address, int count) {
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

CoilBits ModbusClientHandler::readDiscreteInputs(int slave_id, int start_address, int count) {
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

bool ModbusClientHandler::writeSingleRegister(int slave_id, int address, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) {
        return false;
    }
    auto ret = client_->writeSingleRegister(slave_id, address, static_cast<uint16_t>(value));
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

bool ModbusClientHandler::writeMultipleRegisters(int slave_id, int start_address,
                                                  const std::vector<int>& values) {
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

bool ModbusClientHandler::writeSingleCoil(int slave_id, int address, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) {
        return false;
    }
    auto ret = client_->writeSingleCoil(slave_id, address, value);
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

bool ModbusClientHandler::writeMultipleCoils(int slave_id, int start_address,
                                             const CoilBits& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected() || values.size() == 0) {
        return false;
    }
    auto ret = client_->writeMultipleCoils(slave_id, start_address, values.data(),
                                             static_cast<int>(values.size()));
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

int ModbusClientHandler::extractBitValue(int value, int bit_offset) const {
    return (value >> bit_offset) & 1;
}

int ModbusClientHandler::applyBitValue(int current_value, int bit_offset, bool bit_value) const {
    if (bit_value) {
        return current_value | (1 << bit_offset);
    } else {
        return current_value & ~(1 << bit_offset);
    }
}

int ModbusClientHandler::parseAddress(const std::string& address_str) const {
    if (address_str.empty()) {
        return 0;
    }
    if (address_str.length() > 2 &&
        (address_str.substr(0, 2) == "0x" || address_str.substr(0, 2) == "0X")) {
        return std::stoi(address_str, nullptr, 16);
    }
    return std::stoi(address_str);
}

int ModbusClientHandler::getRegisterCount(const std::string& decode) const {
    if (decode.empty()) return 1;
    if (decode == "0x41" || decode == "AB" || decode == "BA") return 1;
    if (decode == "0x42" || decode == "ABCD" || decode == "CDAB" ||
        decode == "BADC" || decode == "DCBA") return 2;
    if (decode == "0x43" || decode == "ABCDEFGH") return 4;
    return 1;
}

bool ModbusClientHandler::valueToInt(const ValueType& value, int& result) const {
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
