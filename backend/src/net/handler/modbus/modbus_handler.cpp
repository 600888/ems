/**
 * @file modbus_handler.cpp
 * @brief Modbus 协议处理器实现（设备级 + 采集插件级合并）
 */
#include "modbus_handler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

#include "net/protocol/encoding_types.h"
#include "net/protocol/modbus/modbus_limits.h"
#include "net/protocol/serial_types.h"
#include "net/protocol/time_units.h"

namespace ems {
namespace net {
namespace device {
using modbus::Defaults;
using modbus::FunctionCode;
using modbus::Layout;
using modbus::Limits;
using modbus::PointField;
using protocol::NumberBase;
using protocol::NumericText;
using protocol::SerialDefaults;
using protocol::TimeUnits;

// ============================================================
// ModbusServerHandler
// ============================================================

ModbusServerHandler::ModbusServerHandler()
    : netType_(modbus::ModbusServer::NetType::TCP),
      connection_("127.0.0.1"),
      settings_(std::to_string(enum_value(Defaults::TcpPort))) {}

ModbusServerHandler::ModbusServerHandler(Logger logger)
    : logger_(logger),
      netType_(modbus::ModbusServer::NetType::TCP),
      connection_("127.0.0.1"),
      settings_(std::to_string(enum_value(Defaults::TcpPort))) {}

ModbusServerHandler::~ModbusServerHandler() { stop(); }

void ModbusServerHandler::initialize(const std::unordered_map<std::string, ValueType>& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_port = config.find("port");
    if (it_port != config.end() && std::get_if<int>(&it_port->second))
        port_ = *std::get_if<int>(&it_port->second);
    if (slaveIdList_.empty()) slaveIdList_ = {1};
    auto it_proto = config.find("protocol_type");
    if (it_proto != config.end() && std::get_if<int>(&it_proto->second))
        netType_ = (*std::get_if<int>(&it_proto->second) == 0) ? modbus::ModbusServer::NetType::RTU
                                                               : modbus::ModbusServer::NetType::TCP;
    if (netType_ == modbus::ModbusServer::NetType::RTU) {
        connection_ = "/dev/ttyUSB0";
        int baudrate = enum_value(SerialDefaults::BaudRate);
        std::string parity = "N";
        int stopbits = 1;
        auto it_s = config.find("serial_port");
        if (it_s != config.end() && std::get_if<std::string>(&it_s->second))
            connection_ = *std::get_if<std::string>(&it_s->second);
        auto it_b = config.find("baudrate");
        if (it_b != config.end() && std::get_if<int>(&it_b->second))
            baudrate = *std::get_if<int>(&it_b->second);
        auto it_p = config.find("parity");
        if (it_p != config.end() && std::get_if<std::string>(&it_p->second))
            parity = *std::get_if<std::string>(&it_p->second);
        auto it_st = config.find("stopbits");
        if (it_st != config.end() && std::get_if<int>(&it_st->second))
            stopbits = *std::get_if<int>(&it_st->second);
        settings_ = std::to_string(baudrate) + parity + std::to_string(stopbits);
    } else {
        connection_ = "*";
        settings_ = std::to_string(port_);
    }
    server_ = std::make_shared<modbus::ModbusServer>(netType_, connection_, settings_);
    for (int sid : slaveIdList_) server_->addSlave(sid);
    server_->setDataChangeCallback([this](int slaveId, Modbus::Table, int addr, int) {
        onModbusWriteInternal(slaveId, enum_value(FunctionCode::ReadHoldingRegisters), addr, {0});
    });
}

std::future<bool> ModbusServerHandler::start() {
    return std::async(std::launch::async, [this]() {
        return server_ ? (running_ = serverStarted_ = server_->start()) : false;
    });
}

std::future<bool> ModbusServerHandler::stop() {
    return std::async(std::launch::async, [this]() {
        if (server_) server_->stop();
        running_ = serverStarted_ = false;
        return true;
    });
}

ValueType ModbusServerHandler::readValue(const std::shared_ptr<ProtocolPoint>& point) {
    return point ? getValueByAddress(point->func_code, point->rtu_addr, point->address)
                 : ValueType{};
}

bool ModbusServerHandler::writeValue(const std::shared_ptr<ProtocolPoint>& point,
                                     const ValueType& value) {
    if (!point) return false;
    setValueByAddress(point->func_code, point->rtu_addr, point->address, value);
    return true;
}

void ModbusServerHandler::addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) {
            points_.push_back(p);
            if (server_) {
                server_->addSlave(p->rtu_addr);
                if (std::find(slaveIdList_.begin(), slaveIdList_.end(), p->rtu_addr) ==
                    slaveIdList_.end())
                    slaveIdList_.push_back(p->rtu_addr);
            }
        }
    }
}

ValueType ModbusServerHandler::getValueByAddress(int funcCode, int slaveId, int address) {
    if (!server_) return {};
    bool bval = false;
    uint16_t uval = 0;
    using EC = modbus::ModbusServer::ErrorCode;
    switch (funcCode) {
        case enum_value(FunctionCode::ReadCoils):
            return (server_->readCoil(slaveId, address, bval) == EC::Success) ? (bval ? 1 : 0)
                                                                              : ValueType{};
        case enum_value(FunctionCode::ReadDiscreteInputs):
            return (server_->readDiscreteInput(slaveId, address, bval) == EC::Success)
                       ? (bval ? 1 : 0)
                       : ValueType{};
        case enum_value(FunctionCode::ReadHoldingRegisters):
            return (server_->readHoldingRegister(slaveId, address, uval) == EC::Success)
                       ? static_cast<int>(uval)
                       : ValueType{};
        case enum_value(FunctionCode::ReadInputRegisters):
            return (server_->readInputRegister(slaveId, address, uval) == EC::Success)
                       ? static_cast<int>(uval)
                       : ValueType{};
        default:
            return {};
    }
}

void ModbusServerHandler::setValueByAddress(int funcCode, int slaveId, int address,
                                            const ValueType& value) {
    if (!server_) return;
    int val = 0;
    valueToInt(value, val);
    switch (funcCode) {
        case enum_value(FunctionCode::ReadCoils):
        case enum_value(FunctionCode::WriteSingleCoil):
            server_->writeCoil(slaveId, address, val != 0);
            break;
        case enum_value(FunctionCode::ReadHoldingRegisters):
        case enum_value(FunctionCode::WriteSingleRegister):
            server_->writeHoldingRegister(slaveId, address, static_cast<uint16_t>(val));
            break;
        case enum_value(FunctionCode::ReadInputRegisters):
            server_->writeInputRegister(slaveId, address, static_cast<uint16_t>(val));
            break;
        default:
            break;
    }
}

ValueType ModbusServerHandler::getValueByAddressWithDecode(int, int, int, const std::string&) {
    return {};
}

void ModbusServerHandler::setValueByAddressWithDecode(int, int, int, const ValueType&,
                                                      const std::string&) {}

std::vector<CapturedMessage> ModbusServerHandler::getCapturedMessages(int limit) const {
    return getMessageCapture() ? getMessageCapture()->getMessages(limit)
                               : std::vector<CapturedMessage>{};
}

void ModbusServerHandler::clearCapturedMessages() {
    if (getMessageCapture()) getMessageCapture()->clear();
}

AvgTimeStats ModbusServerHandler::getAvgTime() const {
    return getMessageCapture() ? getMessageCapture()->getAvgTime() : AvgTimeStats{};
}

void ModbusServerHandler::setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback) {
    writeCallback_ = callback;
}

void ModbusServerHandler::onModbusWriteInternal(int slaveId, int funcCode, int address,
                                                const std::vector<int>& values) {
    if (writeCallback_) writeCallback_->onModbusClientWrite(slaveId, funcCode, address, values);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points_)
        if (p->rtu_addr == slaveId && p->func_code == funcCode && p->address == address &&
            !values.empty())
            p->register_cnt = static_cast<int>(values.size());
}

int ModbusServerHandler::parseAddress(const std::string& s) const {
    if (s.empty()) return 0;
    return (s.size() > enum_value(NumericText::HexPrefixLength) &&
            (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
               ? std::stoi(s, nullptr, enum_value(NumberBase::Hexadecimal))
               : std::stoi(s);
}

int ModbusServerHandler::getRegisterCount(const std::string& decode) const {
    if (decode.empty() || decode == "0x41" || decode == "AB" || decode == "BA")
        return enum_value(Layout::SingleRegisters);
    if (decode == "0x42" || decode == "ABCD" || decode == "CDAB" || decode == "BADC" ||
        decode == "DCBA")
        return enum_value(Layout::DoubleRegisters);
    if (decode == "0x43" || decode == "ABCDEFGH") return enum_value(Layout::QuadRegisters);
    return enum_value(Layout::SingleRegisters);
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
        }
    }
    return false;
}

// ============================================================
// ModbusClientHandler
// ============================================================

ModbusClientHandler::ModbusClientHandler()
    : netType_(modbus::ModbusClient::NetType::TCP),
      connection_("127.0.0.1"),
      settings_(std::to_string(enum_value(Defaults::TcpPort))) {}

ModbusClientHandler::ModbusClientHandler(Logger logger)
    : logger_(logger),
      netType_(modbus::ModbusClient::NetType::TCP),
      connection_("127.0.0.1"),
      settings_(std::to_string(enum_value(Defaults::TcpPort))) {}

ModbusClientHandler::~ModbusClientHandler() { disconnect(); }

void ModbusClientHandler::initialize(const std::unordered_map<std::string, ValueType>& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it_ip = config.find("ip");
    if (it_ip != config.end() && std::get_if<std::string>(&it_ip->second))
        connection_ = *std::get_if<std::string>(&it_ip->second);
    else {
        auto it_h = config.find("host");
        if (it_h != config.end() && std::get_if<std::string>(&it_h->second))
            connection_ = *std::get_if<std::string>(&it_h->second);
    }
    int port = enum_value(Defaults::TcpPort);
    auto it_p = config.find("port");
    if (it_p != config.end() && std::get_if<int>(&it_p->second))
        port = *std::get_if<int>(&it_p->second);
    settings_ = std::to_string(port);

    auto it_pr = config.find("protocol_type");
    if (it_pr != config.end() && std::get_if<int>(&it_pr->second))
        netType_ = (*std::get_if<int>(&it_pr->second) == 0) ? modbus::ModbusClient::NetType::RTU
                                                            : modbus::ModbusClient::NetType::TCP;

    if (netType_ == modbus::ModbusClient::NetType::RTU) {
        connection_ = "/dev/ttyUSB0";
        int br = enum_value(SerialDefaults::BaudRate);
        std::string pa = "N";
        int db = enum_value(SerialDefaults::DataBits);
        int sb = 1;
        auto it_s = config.find("serial_port");
        if (it_s != config.end() && std::get_if<std::string>(&it_s->second))
            connection_ = *std::get_if<std::string>(&it_s->second);
        if (auto* v = std::get_if<int>(&config.find("baudrate")->second)) br = *v;
        if (auto* v = std::get_if<std::string>(&config.find("parity")->second)) pa = *v;
        if (auto* v = std::get_if<int>(&config.find("stopbits")->second)) sb = *v;
        settings_ =
            std::to_string(br) + " " + pa + " " + std::to_string(db) + " " + std::to_string(sb);
    }
    auto it_sl = config.find("slave_id");
    if (it_sl != config.end() && std::get_if<int>(&it_sl->second))
        defaultSlaveId_ = *std::get_if<int>(&it_sl->second);
    auto it_t = config.find("response_timeout");
    if (it_t != config.end() && std::get_if<int>(&it_t->second))
        responseTimeoutMs_ = *std::get_if<int>(&it_t->second);

    client_ = std::make_shared<modbus::ModbusClient>(netType_, connection_, settings_);
    client_->setResponseTimeout(responseTimeoutMs_);
    clientInitialized_ = true;
}

std::future<bool> ModbusClientHandler::start() {
    return std::async(std::launch::async, [this]() { return connect(); });
}

std::future<bool> ModbusClientHandler::stop() {
    return std::async(std::launch::async, [this]() {
        disconnect();
        return true;
    });
}

bool ModbusClientHandler::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_) return false;
    try {
        return (connected_ = running_ = client_->connect());
    } catch (const std::exception& e) {
        if (logger_) {
            std::ostringstream o;
            o << "Modbus 连接失败: " << e.what();
            logger_(o.str());
        }
        return false;
    }
}

void ModbusClientHandler::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (client_) client_->disconnect();
    connected_ = running_ = false;
}

bool ModbusClientHandler::isConnected() const {
    return connected_ && client_ && client_->isConnected();
}

ValueType ModbusClientHandler::readValue(const std::shared_ptr<ProtocolPoint>& point) {
    if (!client_ || !isConnected() || !point) return {};
    return readValueInternal(point->func_code, point->rtu_addr, point->address, point->decode);
}

ValueType ModbusClientHandler::readValueInternal(int funcCode, int slaveId, int address,
                                                 const std::string&) {
    modbus::ModbusClient::ErrorCode ret;
    switch (funcCode) {
        case enum_value(FunctionCode::ReadCoils): {
            bool v;
            ret = client_->readCoils(slaveId, address, &v, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) return v ? 1 : 0;
            break;
        }
        case enum_value(FunctionCode::ReadDiscreteInputs): {
            bool v;
            ret = client_->readDiscreteInputs(slaveId, address, &v, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) return v ? 1 : 0;
            break;
        }
        case enum_value(FunctionCode::ReadHoldingRegisters): {
            uint16_t v;
            ret = client_->readHoldingRegisters(slaveId, address, &v, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) return static_cast<int>(v);
            break;
        }
        case enum_value(FunctionCode::ReadInputRegisters): {
            uint16_t v;
            ret = client_->readInputRegisters(slaveId, address, &v, 1);
            if (ret == modbus::ModbusClient::ErrorCode::Success) return static_cast<int>(v);
            break;
        }
    }
    return {};
}

bool ModbusClientHandler::writeValue(const std::shared_ptr<ProtocolPoint>& point,
                                     const ValueType& value) {
    if (!client_ || !isConnected() || !point) return false;
    int sid = point->rtu_addr, addr = point->address, fc = point->func_code;
    modbus::ModbusClient::ErrorCode ret;
    int vi = 0;
    if (valueToInt(value, vi)) {
        switch (fc) {
            case enum_value(FunctionCode::WriteSingleCoil):
                ret = client_->writeSingleCoil(sid, addr, vi != 0);
                break;
            case enum_value(FunctionCode::WriteSingleRegister):
                ret = client_->writeSingleRegister(sid, addr, static_cast<uint16_t>(vi));
                break;
            case enum_value(FunctionCode::WriteMultipleCoils): {
                CoilBits cb(1);
                cb.set(0, vi != 0);
                ret = client_->writeMultipleCoils(sid, addr, cb.data(), 1);
                break;
            }
            case enum_value(FunctionCode::WriteMultipleRegisters): {
                std::vector<uint16_t> r(1, static_cast<uint16_t>(vi));
                ret = client_->writeMultipleRegisters(sid, addr, r.data(), 1);
                break;
            }
            default:
                ret = client_->writeSingleRegister(sid, addr, static_cast<uint16_t>(vi));
                break;
        }
    } else if (auto* b = std::get_if<bool>(&value)) {
        ret = (fc == enum_value(FunctionCode::WriteSingleCoil) ||
               fc == enum_value(FunctionCode::ReadCoils))
                  ? client_->writeSingleCoil(sid, addr, *b)
                  : client_->writeSingleRegister(sid, addr, *b ? 1 : 0);
    } else
        ret = modbus::ModbusClient::ErrorCode::InvalidData;
    return ret == modbus::ModbusClient::ErrorCode::Success;
}

void ModbusClientHandler::addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : points) {
        if (pointsSet_.insert(p).second) points_.push_back(p);
    }
}

std::vector<CapturedMessage> ModbusClientHandler::getCapturedMessages(int) const { return {}; }

void ModbusClientHandler::clearCapturedMessages() {}

AvgTimeStats ModbusClientHandler::getAvgTime() const { return {}; }

// ---- 批量读取 ----
std::vector<int> ModbusClientHandler::readRegistersBatchAsync(int fc, int sid, int sa, int cnt) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) return {};
    std::vector<uint16_t> vals(cnt);
    modbus::ModbusClient::ErrorCode ret;
    if (fc == enum_value(FunctionCode::ReadHoldingRegisters) ||
        fc == enum_value(FunctionCode::WriteSingleRegister) ||
        fc == enum_value(FunctionCode::WriteMultipleRegisters))
        ret = client_->readHoldingRegisters(sid, sa, vals.data(), cnt);
    else if (fc == enum_value(FunctionCode::ReadInputRegisters))
        ret = client_->readInputRegisters(sid, sa, vals.data(), cnt);
    else
        return {};
    if (ret != modbus::ModbusClient::ErrorCode::Success) return {};
    std::vector<int> r;
    r.reserve(cnt);
    for (int i = 0; i < cnt; ++i) r.push_back(static_cast<int>(vals[i]));
    return r;
}

std::vector<int> ModbusClientHandler::readHoldingRegisters(int sid, int sa, int cnt) {
    return readRegistersBatchAsync(enum_value(FunctionCode::ReadHoldingRegisters), sid, sa, cnt);
}

std::vector<int> ModbusClientHandler::readInputRegisters(int sid, int sa, int cnt) {
    return readRegistersBatchAsync(enum_value(FunctionCode::ReadInputRegisters), sid, sa, cnt);
}

CoilBits ModbusClientHandler::readCoils(int sid, int sa, int cnt) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) return {};
    CoilBits v(cnt);
    if (client_->readCoils(sid, sa, v.data(), cnt) == modbus::ModbusClient::ErrorCode::Success)
        return v;
    return {};
}

CoilBits ModbusClientHandler::readDiscreteInputs(int sid, int sa, int cnt) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected()) return {};
    CoilBits v(cnt);
    if (client_->readDiscreteInputs(sid, sa, v.data(), cnt) ==
        modbus::ModbusClient::ErrorCode::Success)
        return v;
    return {};
}

bool ModbusClientHandler::writeSingleRegister(int sid, int addr, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && isConnected() &&
           client_->writeSingleRegister(sid, addr, static_cast<uint16_t>(value)) ==
               modbus::ModbusClient::ErrorCode::Success;
}

bool ModbusClientHandler::writeMultipleRegisters(int sid, int sa, const std::vector<int>& vals) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!client_ || !isConnected() || vals.empty()) return false;
    std::vector<uint16_t> r(vals.size());
    for (size_t i = 0; i < vals.size(); ++i) r[i] = static_cast<uint16_t>(vals[i]);
    return client_->writeMultipleRegisters(sid, sa, r.data(), static_cast<int>(r.size())) ==
           modbus::ModbusClient::ErrorCode::Success;
}

bool ModbusClientHandler::writeSingleCoil(int sid, int addr, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && isConnected() &&
           client_->writeSingleCoil(sid, addr, value) == modbus::ModbusClient::ErrorCode::Success;
}

bool ModbusClientHandler::writeMultipleCoils(int sid, int sa, const CoilBits& vals) {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_ && isConnected() && vals.size() > 0 &&
           client_->writeMultipleCoils(sid, sa, vals.data(), static_cast<int>(vals.size())) ==
               modbus::ModbusClient::ErrorCode::Success;
}

// ============================================================
// 插件级: 测点解析
// ============================================================

ParsedPoint ModbusClientHandler::parse_point_code(const std::string& code) {
    ParsedPoint pp;
    pp.originalCode = code;
    std::vector<std::string> p;
    std::istringstream ss(code);
    std::string t;
    while (std::getline(ss, t, ':')) p.push_back(t);
    if (p.size() > enum_value(PointField::Slave))
        pp.slaveId = std::stoi(p[enum_value(PointField::Slave)]);
    if (p.size() > enum_value(PointField::Function))
        pp.funcCode = std::stoi(p[enum_value(PointField::Function)]);
    if (p.size() > enum_value(PointField::Address)) {
        auto& a = p[enum_value(PointField::Address)];
        pp.address = (a.size() > enum_value(NumericText::HexPrefixLength) && a[0] == '0' &&
                      (a[1] == 'x' || a[1] == 'X'))
                         ? std::stoi(a, nullptr, enum_value(NumberBase::Hexadecimal))
                         : std::stoi(a);
    }
    if (p.size() > enum_value(PointField::Decode)) {
        int d = std::stoi(p[enum_value(PointField::Decode)]);
        if (d >= 0 && d <= static_cast<int>(DecodeCode::BIT))
            pp.decode = static_cast<DecodeCode>(d);
    }
    if (p.size() > enum_value(PointField::Endian)) {
        int e = std::stoi(p[enum_value(PointField::Endian)]);
        if (e >= 0 && e <= static_cast<int>(EndianCode::LittleEndianByteSwap))
            pp.endian = static_cast<EndianCode>(e);
    }
    pp.regCount = model::decode_reg_count(pp.decode);
    return pp;
}

std::vector<MergedRange> ModbusClientHandler::merge_ranges(
    const std::vector<ParsedPoint>& pts) const {
    struct K {
        int sid, fc;
        auto operator<=>(const K&) const = default;
    };

    std::map<K, std::vector<size_t>> g;
    for (size_t i = 0; i < pts.size(); ++i) g[{pts[i].slaveId, pts[i].funcCode}].push_back(i);
    std::vector<MergedRange> r;
    for (auto& [k, idx] : g) {
        std::sort(idx.begin(), idx.end(),
                  [&](size_t a, size_t b) { return pts[a].address < pts[b].address; });
        MergedRange cur;
        cur.slaveId = k.sid;
        cur.funcCode = k.fc;
        for (size_t i : idx) {
            const auto& pt = pts[i];
            int pe = pt.address + pt.regCount - 1;
            if (cur.pointCount == 0) {
                cur.startAddr = pt.address;
                cur.count = pt.regCount;
                cur.pointStartIdx = static_cast<int>(i);
                cur.pointCount = 1;
            } else {
                int ce = cur.startAddr + cur.count - 1;
                if (pt.address <= ce + 1 && pt.address >= cur.startAddr) {
                    cur.count = std::max(ce, pe) - cur.startAddr + 1;
                    cur.pointCount++;
                } else {
                    r.push_back(cur);
                    cur.startAddr = pt.address;
                    cur.count = pt.regCount;
                    cur.pointStartIdx = static_cast<int>(i);
                    cur.pointCount = 1;
                }
            }
        }
        if (cur.pointCount > 0) r.push_back(cur);
    }
    return r;
}

// ============================================================
// 插件级: 端序引擎
// ============================================================

void ModbusClientHandler::assemble_bytes(const uint16_t* regs, int regCount, EndianCode endian,
                                         uint8_t* out) {
    uint8_t flat[enum_value(Layout::LegacyScratchBytes)];
    for (int i = 0; i < regCount; ++i) {
        flat[i * enum_value(Layout::BytesPerRegister)] =
            (regs[i] >> enum_value(Layout::BitsPerByte)) & enum_value(Layout::ByteMask);
        flat[i * enum_value(Layout::BytesPerRegister) + 1] = regs[i] & enum_value(Layout::ByteMask);
    }
    int bc = regCount * enum_value(Layout::BytesPerRegister);
    switch (endian) {
        case EndianCode::BigEndian:
            std::memcpy(out, flat, bc);
            break;
        case EndianCode::LittleEndian:
            for (int i = 0; i < regCount; ++i)
                std::memcpy(out + i * enum_value(Layout::BytesPerRegister),
                            flat + (regCount - 1 - i) * enum_value(Layout::BytesPerRegister),
                            enum_value(Layout::BytesPerRegister));
            break;
        case EndianCode::BigEndianByteSwap:
            for (int i = 0; i < regCount; ++i) {
                out[i * enum_value(Layout::BytesPerRegister)] =
                    flat[i * enum_value(Layout::BytesPerRegister) + 1];
                out[i * enum_value(Layout::BytesPerRegister) + 1] =
                    flat[i * enum_value(Layout::BytesPerRegister)];
            }
            break;
        case EndianCode::LittleEndianByteSwap:
            for (int i = 0; i < regCount; ++i) {
                int s = (regCount - 1 - i) * enum_value(Layout::BytesPerRegister);
                out[i * enum_value(Layout::BytesPerRegister)] = flat[s + 1];
                out[i * enum_value(Layout::BytesPerRegister) + 1] = flat[s];
            }
            break;
    }
}

void ModbusClientHandler::disassemble_bytes(const uint8_t* data, int bc, EndianCode endian,
                                            uint16_t* out) {
    int rc = bc / enum_value(Layout::BytesPerRegister);
    uint8_t flat[enum_value(Layout::LegacyScratchBytes)];
    std::memcpy(flat, data, bc);
    switch (endian) {
        case EndianCode::BigEndian:
            for (int i = 0; i < rc; ++i)
                out[i] = (flat[i * enum_value(Layout::BytesPerRegister)]
                          << enum_value(Layout::BitsPerByte)) |
                         flat[i * enum_value(Layout::BytesPerRegister) + 1];
            break;
        case EndianCode::LittleEndian:
            for (int i = 0; i < rc; ++i) {
                int s = (rc - 1 - i) * enum_value(Layout::BytesPerRegister);
                out[i] = (flat[s] << enum_value(Layout::BitsPerByte)) | flat[s + 1];
            }
            break;
        case EndianCode::BigEndianByteSwap:
            for (int i = 0; i < rc; ++i)
                out[i] = (flat[i * enum_value(Layout::BytesPerRegister) + 1]
                          << enum_value(Layout::BitsPerByte)) |
                         flat[i * enum_value(Layout::BytesPerRegister)];
            break;
        case EndianCode::LittleEndianByteSwap:
            for (int i = 0; i < rc; ++i) {
                int s = (rc - 1 - i) * enum_value(Layout::BytesPerRegister);
                out[i] = (flat[s + 1] << enum_value(Layout::BitsPerByte)) | flat[s];
            }
            break;
    }
}

ProtocolHandler::PointValue ModbusClientHandler::read_point(const ParsedPoint& pp,
                                                            const uint16_t* raw, int rawLen) {
    double val = 0;
    int64_t rv = 0;
    int q = (rawLen >= pp.regCount) ? enum_value(model::PointQuality::Good)
                                    : enum_value(model::PointQuality::Invalid);
    uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    if (q == enum_value(model::PointQuality::Good) &&
        (pp.funcCode == enum_value(FunctionCode::ReadCoils) ||
         pp.funcCode == enum_value(FunctionCode::ReadDiscreteInputs))) {
        int wi = pp.address / enum_value(Layout::BitsPerRegister),
            bi = pp.bitOffset >= 0 ? pp.bitOffset
                                   : (pp.address % enum_value(Layout::BitsPerRegister));
        if (wi < rawLen) {
            bool bv = (raw[wi] >> bi) & 1;
            val = bv ? 1 : 0;
            rv = bv ? 1 : 0;
        } else
            q = enum_value(model::PointQuality::Invalid);
    } else if (q == enum_value(model::PointQuality::Good)) {
        uint8_t bytes[enum_value(Layout::MaxValueBytes)] = {0};
        assemble_bytes(raw, pp.regCount, pp.endian, bytes);
        int bc = pp.regCount * enum_value(Layout::BytesPerRegister);
        switch (pp.decode) {
            case DecodeCode::UINT16: {
                uint16_t v;
                std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = v;
                break;
            }
            case DecodeCode::INT16: {
                int16_t v;
                std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = v;
                break;
            }
            case DecodeCode::UINT32: {
                uint32_t v = 0;
                if (bc >= static_cast<int>(sizeof(v))) std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = v;
                break;
            }
            case DecodeCode::INT32: {
                int32_t v = 0;
                if (bc >= static_cast<int>(sizeof(v))) std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = v;
                break;
            }
            case DecodeCode::FLOAT: {
                float v = 0;
                if (bc >= static_cast<int>(sizeof(v))) std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = *reinterpret_cast<const uint32_t*>(bytes);
                break;
            }
            case DecodeCode::DOUBLE: {
                double v = 0;
                if (bc >= static_cast<int>(sizeof(v))) std::memcpy(&v, bytes, sizeof(v));
                val = v;
                rv = *reinterpret_cast<const uint64_t*>(bytes);
                break;
            }
            case DecodeCode::UINT8:
                val = bytes[0];
                rv = bytes[0];
                break;
            case DecodeCode::INT8: {
                int8_t v = static_cast<int8_t>(bytes[0]);
                val = v;
                rv = v;
                break;
            }
            case DecodeCode::BOOL:
            case DecodeCode::BIT: {
                bool bv = (bytes[0] & enum_value(Layout::LeastSignificantBit)) != 0;
                val = bv ? 1.0 : 0.0;
                rv = bv ? 1 : 0;
                break;
            }
        }
    }
    return {pp.originalCode, val, rv, q, ts};
}

// ============================================================
// 插件级: read_batch
// ============================================================

int ModbusClientHandler::read_batch(const std::vector<std::string>& codes,
                                    std::vector<ProtocolHandler::PointValue>& results) {
    std::lock_guard<std::mutex> lock(mutex_);
    results.clear();
    if (codes.empty()) return 0;
    if (!client_ || !isConnected()) {
        last_err_ = "Not connected";
        return -1;
    }
    std::vector<ParsedPoint> pts;
    pts.reserve(codes.size());
    for (const auto& c : codes) pts.push_back(parse_point_code(c));
    auto ranges = merge_ranges(pts);
    results.assign(codes.size(), ProtocolHandler::PointValue{});
    constexpr int MB = enum_value(Limits::LegacyBatchBufferRegisters);
    uint16_t buf[MB];
    int ok = 0;
    for (const auto& rg : ranges) {
        int mpr = (rg.funcCode == enum_value(FunctionCode::ReadCoils) ||
                   rg.funcCode == enum_value(FunctionCode::ReadDiscreteInputs))
                      ? enum_value(Limits::MaxReadCoils)
                      : enum_value(Limits::MaxReadRegisters);
        int off = 0;
        while (off < rg.count) {
            int cnt = std::min(rg.count - off, mpr), addr = rg.startAddr + off;
            std::memset(buf, 0, sizeof(buf));
            auto err = read_range_data(rg, buf, cnt);
            if (err != modbus::ModbusClient::ErrorCode::Success) {
                off += cnt;
                continue;
            }
            int re = rg.pointStartIdx + rg.pointCount;
            for (int pi = rg.pointStartIdx; pi < re; ++pi) {
                if (pi >= static_cast<int>(pts.size())) break;
                const auto& pp = pts[pi];
                if (pp.address < addr || pp.address >= addr + cnt) continue;
                int bo = pp.address - addr;
                if (bo < 0 || bo >= cnt) continue;
                auto pv = read_point(pp, buf + bo, cnt - bo);
                if (std::get<3>(pv) == enum_value(model::PointQuality::Good)) ok++;
                results[pi] = std::move(pv);
            }
            off += cnt;
        }
    }
    return ok;
}

modbus::ModbusClient::ErrorCode ModbusClientHandler::read_range_data(const MergedRange& rg,
                                                                     uint16_t* buf, int cnt) {
    if (!client_) return modbus::ModbusClient::ErrorCode::Unknown;
    switch (rg.funcCode) {
        case enum_value(FunctionCode::ReadCoils):
            return client_->readCoils(rg.slaveId, rg.startAddr, reinterpret_cast<bool*>(buf), cnt);
        case enum_value(FunctionCode::ReadDiscreteInputs):
            return client_->readDiscreteInputs(rg.slaveId, rg.startAddr,
                                               reinterpret_cast<bool*>(buf), cnt);
        case enum_value(FunctionCode::ReadHoldingRegisters):
            return client_->readHoldingRegisters(rg.slaveId, rg.startAddr, buf, cnt);
        case enum_value(FunctionCode::ReadInputRegisters):
            return client_->readInputRegisters(rg.slaveId, rg.startAddr, buf, cnt);
        default:
            return modbus::ModbusClient::ErrorCode::InvalidAddress;
    }
}

int ModbusClientHandler::extractBitValue(int v, int bo) const { return (v >> bo) & 1; }

int ModbusClientHandler::applyBitValue(int cv, int bo, bool bv) const {
    return bv ? (cv | (1 << bo)) : (cv & ~(1 << bo));
}

int ModbusClientHandler::parseAddress(const std::string& s) const {
    if (s.empty()) return 0;
    return (s.size() > enum_value(NumericText::HexPrefixLength) && s[0] == '0' &&
            (s[1] == 'x' || s[1] == 'X'))
               ? std::stoi(s, nullptr, enum_value(NumberBase::Hexadecimal))
               : std::stoi(s);
}

int ModbusClientHandler::getRegisterCount(const std::string& d) const {
    if (d.empty() || d == "0x41" || d == "AB" || d == "BA")
        return enum_value(Layout::SingleRegisters);
    if (d == "0x42" || d == "ABCD" || d == "CDAB" || d == "BADC" || d == "DCBA")
        return enum_value(Layout::DoubleRegisters);
    if (d == "0x43") return enum_value(Layout::QuadRegisters);
    return enum_value(Layout::SingleRegisters);
}

bool ModbusClientHandler::valueToInt(const ValueType& v, int& r) const {
    if (auto* p = std::get_if<int>(&v)) {
        r = *p;
        return true;
    }
    if (auto* p = std::get_if<int64_t>(&v)) {
        r = static_cast<int>(*p);
        return true;
    }
    if (auto* p = std::get_if<double>(&v)) {
        r = static_cast<int>(std::round(*p));
        return true;
    }
    if (auto* p = std::get_if<bool>(&v)) {
        r = *p ? 1 : 0;
        return true;
    }
    if (auto* p = std::get_if<std::string>(&v)) {
        try {
            r = std::stoi(*p);
            return true;
        } catch (...) {
        }
    }
    return false;
}

}  // namespace device
}  // namespace net
}  // namespace ems
