/**
 * @file base_handler.h
 * @brief 规约处理器基类定义
 *
 * 所有协议处理器（Modbus/IEC104/DLT645/IEC61850）统一继承此基类。
 * 提供统一的 ValueType、ProtocolPoint、MessageCapture 等类型定义，
 * 以及 ProtocolHandler/ServerHandler/ClientHandler 三层抽象。
 *
 * 同时集成了 plugin 级接口（read_batch、connect/disconnect）
 * 和端序引擎（供多寄存器类型解码使用）。
 *
 * @note 所在目录: net/protocol/handler/ (由 net/device/protocol_handler/ 迁移而来)
 */
#ifndef EMS_NET_PROTOCOL_HANDLER_BASE_H
#define EMS_NET_PROTOCOL_HANDLER_BASE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <future>
#include <atomic>
#include <optional>
#include <cstdint>
#include "models/base.h"

namespace ems {
namespace net {
namespace device {

using ValueType = std::variant<int, int64_t, double, bool, std::string>;

/**
 * @brief 规约处理器可见的测点数据
 */
struct ProtocolPoint {
    int         rtu_addr      = 1;
    int         address       = 0;
    int         func_code     = 3;
    std::string decode        = "0x41";
    int         bit           = -1;
    int         register_cnt  = 1;
    virtual ~ProtocolPoint() = default;
};

struct CapturedMessage {
    std::string direction;
    std::vector<uint8_t> data;
    uint64_t timestamp;
    std::string time;
    std::string description;
};

struct AvgTimeStats {
    int tx_count{0};
    int rx_count{0};
    double avg_tx_interval_ms{0.0};
    double avg_rx_interval_ms{0.0};
};

class MessageCapture {
public:
    virtual ~MessageCapture() = default;
    virtual void addTx(const std::vector<uint8_t>& data, const std::string& description = "") = 0;
    virtual void addRx(const std::vector<uint8_t>& data, const std::string& description = "") = 0;
    virtual std::vector<CapturedMessage> getMessages(int limit = 100) const = 0;
    virtual void clear() = 0;
    virtual AvgTimeStats getAvgTime() const = 0;
};

/**
 * @brief 协议处理器基类（设备级 + 采集级统一接口）
 */
class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    bool isRunning() const { return running_.load(); }

    std::shared_ptr<MessageCapture> getMessageCapture() const { return messageCapture_; }
    void setMessageCapture(std::shared_ptr<MessageCapture> capture) { messageCapture_ = capture; }

    void addTxMessage(const std::vector<uint8_t>& data, const std::string& description = "");
    void addRxMessage(const std::vector<uint8_t>& data, const std::string& description = "");
    void addTx(const std::vector<uint8_t>& data, const std::string& description = "");
    void addRx(const std::vector<uint8_t>& data, const std::string& description = "");

    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;
    virtual std::future<bool> start() = 0;
    virtual std::future<bool> stop() = 0;

    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;

    std::future<ValueType> readValueAsync(const std::shared_ptr<ProtocolPoint>& point) {
        return std::async(std::launch::async, [this, point]() {
            return readValue(point);
        });
    }

    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) = 0;

    std::future<bool> writeValueAsync(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) {
        return std::async(std::launch::async, [this, point, value]() {
            return writeValue(point, value);
        });
    }

    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;

    void setConfig(const std::string& key, const ValueType& value) { config_[key] = value; }
    std::optional<ValueType> getConfig(const std::string& key) const;

    virtual std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const;
    virtual void clearCapturedMessages();
    virtual AvgTimeStats getAvgTime() const;

    // ---- Plugin 级采集接口 ----
    using PointValue = std::tuple<std::string, double, int64_t, int, uint64_t>;  // code, value, raw, quality, ts
    virtual int  read_batch(const std::vector<std::string>& point_codes,
                            std::vector<PointValue>& results);
    virtual bool is_connected() const { return false; }
    virtual std::string last_error() const { return {}; }

protected:
    ProtocolHandler() = default;

    std::unordered_map<std::string, ValueType> config_;
    std::shared_ptr<MessageCapture> messageCapture_;
    std::atomic<bool> running_{false};
};

class ServerHandler : public ProtocolHandler {
public:
    virtual ~ServerHandler() = default;
    virtual ValueType getValueByAddress(int funcCode, int slaveId, int address) = 0;
    virtual void setValueByAddress(int funcCode, int slaveId, int address, const ValueType& value) = 0;
    virtual std::future<bool> start() = 0;
    virtual std::future<bool> stop() = 0;
    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) = 0;
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;
    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;
};

class ClientHandler : public ProtocolHandler {
public:
    virtual ~ClientHandler() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::future<bool> start() = 0;
    virtual std::future<bool> stop() = 0;
    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) = 0;
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;
    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;
};

} // namespace device
} // namespace net
} // namespace ems

#endif // EMS_NET_PROTOCOL_HANDLER_BASE_H
