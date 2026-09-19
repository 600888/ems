/**
 * @file modbus_handler.h
 * @brief Modbus 协议处理器（设备级 + 采集级统一 | 服务端 + 客户端 + 批量读取 + 端序引擎）
 *
 * 合并自:
 *   - net/device/protocol_handler/modbus_handler.h (原设备级 ServerHandler/ClientHandler)
 *   - protocol/modbus/modbus_proto_handler.h       (原插件级 read_batch/parse_point/端序)
 *
 * 测点格式: "slave_id:func_code:address[:decode_code[:endian_code]]"
 *   decode_code: 0=UINT16, 1=INT16, 2=UINT32, 3=INT32, 4=FLOAT, 5=DOUBLE, ...
 *   endian_code: 0=BigEndian, 1=LittleEndian, 2=BigEndianByteSwap, 3=LittleEndianByteSwap
 */
#ifndef EMS_NET_PROTOCOL_MODBUS_HANDLER_H
#define EMS_NET_PROTOCOL_MODBUS_HANDLER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "modbus_client.h"
#include "modbus_server.h"
#include "models/base.h"
#include "net/protocol/handler/base_handler.h"

namespace ems {
namespace net {
namespace device {

using json = nlohmann::json;
using model::DecodeCode;
using model::EndianCode;

// ============================================================
// 辅助类型
// ============================================================

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
        return (index < count_) && ((data_[index / 8] & (1 << (index % 8))) != 0);
    }
    void set(size_t index, bool value) {
        if (index >= count_) return;
        if (value)
            data_[index / 8] |= (1 << (index % 8));
        else
            data_[index / 8] &= ~(1 << (index % 8));
    }
    bool* data() { return reinterpret_cast<bool*>(data_.data()); }
    const bool* data() const { return reinterpret_cast<const bool*>(data_.data()); }

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

// ============================================================
// 插件级测点解析
// ============================================================

struct ParsedPoint {
    int slaveId{1};
    int funcCode{3};
    int address{0};
    int regCount{1};
    int bitOffset{-1};
    DecodeCode decode{DecodeCode::UINT16};
    EndianCode endian{EndianCode::BigEndian};
    std::string originalCode;
};

struct MergedRange {
    int slaveId{1}, funcCode{3}, startAddr{0}, count{0}, pointStartIdx{0}, pointCount{0};
};

// ============================================================
// ModbusServerHandler
// ============================================================

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
    void setValueByAddress(int funcCode, int slaveId, int address, const ValueType& value) override;
    ValueType getValueByAddressWithDecode(int funcCode, int slaveId, int address,
                                          const std::string& decode);
    void setValueByAddressWithDecode(int funcCode, int slaveId, int address, const ValueType& value,
                                     const std::string& decode);
    std::shared_ptr<modbus::ModbusServer> server() const { return server_; }
    std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const override;
    void clearCapturedMessages() override;
    AvgTimeStats getAvgTime() const override;
    void setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback);
    std::shared_ptr<modbus::ModbusServer> getServer() const { return server_; }

   private:
    void onModbusWriteInternal(int slaveId, int funcCode, int address,
                               const std::vector<int>& values);
    int parseAddress(const std::string& s) const;
    int getRegisterCount(const std::string& decode) const;
    bool valueToInt(const ValueType& v, int& r) const;
    std::shared_ptr<modbus::ModbusServer> server_;
    Logger logger_;
    std::vector<std::shared_ptr<ProtocolPoint>> points_;
    std::vector<int> slaveIdList_;
    std::unordered_set<std::shared_ptr<ProtocolPoint>> pointsSet_;
    std::mutex mutex_;
    std::shared_ptr<ModbusWriteCallback> writeCallback_;
    std::atomic<bool> serverStarted_{false};
    modbus::ModbusServer::NetType netType_;
    std::string connection_, settings_;
    int port_{502};
};

// ============================================================
// ModbusClientHandler (设备级 + 插件级合并)
// ============================================================

class ModbusClientHandler : public ClientHandler {
   public:
    using Logger = std::function<void(const std::string&)>;
    ModbusClientHandler();
    explicit ModbusClientHandler(Logger logger);
    ~ModbusClientHandler() override;

    // ---- 设备级接口 ----
    void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    std::future<bool> start() override;
    std::future<bool> stop() override;
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) override;
    bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) override;
    void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) override;
    std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const override;
    void clearCapturedMessages() override;
    AvgTimeStats getAvgTime() const override;

    // ---- 批量读取 ----
    std::vector<int> readRegistersBatchAsync(int funcCode, int slaveId, int startAddress,
                                             int count);
    std::vector<int> readHoldingRegisters(int slaveId, int startAddress, int count);
    std::vector<int> readInputRegisters(int slaveId, int startAddress, int count);
    CoilBits readCoils(int slaveId, int startAddress, int count);
    CoilBits readDiscreteInputs(int slaveId, int startAddress, int count);
    bool writeSingleRegister(int slaveId, int address, int value);
    bool writeMultipleRegisters(int slaveId, int startAddress, const std::vector<int>& values);
    bool writeSingleCoil(int slaveId, int address, bool value);
    bool writeMultipleCoils(int slaveId, int startAddress, const CoilBits& values);
    std::shared_ptr<modbus::ModbusClient> client() const { return client_; }

    // ---- 插件级接口 ----
    int read_batch(const std::vector<std::string>& point_codes,
                   std::vector<ProtocolHandler::PointValue>& results) override;
    bool is_connected() const override { return isConnected(); }
    std::string last_error() const override { return last_err_; }

    // ---- 测点解析（插件级，供 modbus_plugin.cpp 复用） ----
    static ParsedPoint parse_point_code(const std::string& code);

   private:
    ValueType readValueInternal(int funcCode, int slaveId, int address, const std::string& decode);
    int extractBitValue(int value, int bit_offset) const;
    int applyBitValue(int current_value, int bit_offset, bool bit_value) const;
    int parseAddress(const std::string& s) const;
    int getRegisterCount(const std::string& decode) const;
    bool valueToInt(const ValueType& v, int& r) const;

    // ---- 插件级 ----
    std::vector<MergedRange> merge_ranges(const std::vector<ParsedPoint>& points) const;
    static void assemble_bytes(const uint16_t* regs, int regCount, EndianCode endian, uint8_t* out);
    static void disassemble_bytes(const uint8_t* data, int byteCount, EndianCode endian,
                                  uint16_t* outRegs);
    ProtocolHandler::PointValue read_point(const ParsedPoint& pp, const uint16_t* rawData,
                                           int rawLen);
    modbus::ModbusClient::ErrorCode read_range_data(const MergedRange& range, uint16_t* buf,
                                                    int bufCount);

    std::shared_ptr<modbus::ModbusClient> client_;
    Logger logger_;
    std::vector<std::shared_ptr<ProtocolPoint>> points_;
    std::unordered_set<std::shared_ptr<ProtocolPoint>> pointsSet_;
    std::mutex mutex_;
    std::atomic<bool> connected_{false}, clientInitialized_{false};
    modbus::ModbusClient::NetType netType_;
    std::string connection_, settings_;
    int defaultSlaveId_{1}, responseTimeoutMs_{1000}, byteTimeoutMs_{1000};
    mutable std::string last_err_;
};

}  // namespace device
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_PROTOCOL_MODBUS_HANDLER_H
