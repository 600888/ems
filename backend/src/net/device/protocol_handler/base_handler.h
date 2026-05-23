/**
 * @file base_handler.h
 * @brief 规约处理器基类定义
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_DEVICE_PROTOCOL_HANDLER_BASE_H
#define EMS_NET_DEVICE_PROTOCOL_HANDLER_BASE_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <variant>
#include <future>
#include <atomic>
#include <optional>

namespace ems {
namespace net {
namespace device {

using ValueType = std::variant<int, int64_t, double, bool, std::string>;

/**
 * @brief 规约处理器可见的测点数据
 *
 * 去模板化后，规约处理器统一通过此结构体访问测点的协议相关字段。
 * 不同数据源（RTDB / 测试）的测点只需构造或填充此结构体即可接入。
 */
struct ProtocolPoint {
    int         rtu_addr      = 1;            // 从站地址 / 设备地址
    int         address       = 0;            // 寄存器地址 / 信息体地址
    int         func_code     = 3;            // 功能码
    std::string decode        = "0x41";       // 解析码
    int         bit           = -1;           // 位号 (-1 表示整寄存器)
    int         register_cnt  = 1;            // 占用的寄存器数量

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
    
    virtual std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const = 0;
    virtual void clearCapturedMessages() = 0;
    virtual AvgTimeStats getAvgTime() const = 0;

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

#endif // EMS_NET_DEVICE_PROTOCOL_HANDLER_BASE_H
