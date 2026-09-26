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

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "models/base.h"

namespace ems {
namespace net {
namespace device {

using ValueType = std::variant<int, int64_t, double, bool, std::string>;

/**
 * @brief 规约处理器可见的测点数据
 */
struct ProtocolPoint {
    int rtu_addr = 1;
    int address = 0;
    int func_code = 3;
    std::string decode = "0x41";
    int bit = -1;
    int register_cnt = 1;
    /** @brief 销毁协议测点描述。 */
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

/** @brief 记录协议收发报文和平均间隔。 */
class MessageCapture {
   public:
    /** @brief 销毁报文捕获接口。 */
    virtual ~MessageCapture() = default;

    /**
     * @brief 记录发送报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    virtual void addTx(const std::vector<uint8_t>& data, const std::string& description = "") = 0;

    /**
     * @brief 记录接收报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    virtual void addRx(const std::vector<uint8_t>& data, const std::string& description = "") = 0;

    /**
     * @brief 返回最近的有限数量捕获报文。
     * @param[in] limit 结果数量上限。
     * @return 匹配条件的数据集合。
     */
    virtual std::vector<CapturedMessage> getMessages(int limit = 100) const = 0;

    /** @brief 清空已捕获报文。 */
    virtual void clear() = 0;

    /**
     * @brief 返回发送和接收报文的平均间隔。
     * @return 得到的时间。
     */
    virtual AvgTimeStats getAvgTime() const = 0;
};

/**
 * @brief 协议处理器基类（设备级 + 采集级统一接口）
 */
class ProtocolHandler {
   public:
    /** @brief 销毁协议处理器。 */
    virtual ~ProtocolHandler() = default;

    /**
     * @brief 判断协议处理器是否正在运行。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 获取当前报文捕获器。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<MessageCapture> getMessageCapture() const { return messageCapture_; }

    /**
     * @brief 设置报文捕获器。
     * @param[in] capture 报文捕获器。
     */
    void setMessageCapture(std::shared_ptr<MessageCapture> capture) { messageCapture_ = capture; }

    /**
     * @brief 向捕获器追加发送报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    void addTxMessage(const std::vector<uint8_t>& data, const std::string& description = "");

    /**
     * @brief 向捕获器追加接收报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    void addRxMessage(const std::vector<uint8_t>& data, const std::string& description = "");

    /**
     * @brief 记录发送报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    void addTx(const std::vector<uint8_t>& data, const std::string& description = "");

    /**
     * @brief 记录接收报文。
     * @param[in] data 测点数据。
     * @param[in] description 描述文本。
     */
    void addRx(const std::vector<uint8_t>& data, const std::string& description = "");

    /**
     * @brief 应用协议配置。
     * @param[in] config 配置数据。
     */
    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;

    /**
     * @brief 异步启动协议处理器。
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> start() = 0;

    /**
     * @brief 异步停止协议处理器。
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> stop() = 0;

    /**
     * @brief 读取单个协议测点的值。
     * @param[in] point 测点数据。
     * @return 得到的协议测点值。
     */
    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;

    /**
     * @brief 在独立异步任务中读取单个测点。
     * @param[in] point 测点数据。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<ValueType> readValueAsync(const std::shared_ptr<ProtocolPoint>& point) {
        return std::async(std::launch::async, [this, point]() { return readValue(point); });
    }

    /**
     * @brief 向单个协议测点写值。
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point,
                            const ValueType& value) = 0;

    /**
     * @brief 在独立异步任务中写入单个测点。
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> writeValueAsync(const std::shared_ptr<ProtocolPoint>& point,
                                      const ValueType& value) {
        return std::async(std::launch::async,
                          [this, point, value]() { return writeValue(point, value); });
    }

    /**
     * @brief 向处理器注册一组协议测点。
     * @param[in] points 测点数据列表。
     */
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;

    /**
     * @brief 更新单项协议配置。
     * @param[in] key Redis 键。
     * @param[in] value 待写入的值。
     */
    void setConfig(const std::string& key, const ValueType& value) { config_[key] = value; }

    /**
     * @brief 查找单项协议配置；缺失时返回空值。
     * @param[in] key Redis 键。
     * @return 找到时返回对应值，否则返回 std::nullopt。
     */
    std::optional<ValueType> getConfig(const std::string& key) const;

    /**
     * @brief 返回最近捕获的有限数量报文。
     * @param[in] limit 结果数量上限。
     * @return 匹配条件的数据集合。
     */
    virtual std::vector<CapturedMessage> getCapturedMessages(int limit = 100) const;

    /** @brief 清空已捕获报文。 */
    virtual void clearCapturedMessages();

    /**
     * @brief 返回报文平均间隔统计。
     * @return 得到的时间。
     */
    virtual AvgTimeStats getAvgTime() const;

    /** @brief 插件批量采集结果类型。 */
    using PointValue =
        std::tuple<std::string, double, int64_t, int, uint64_t>;  // code, value, raw, quality, ts

    /**
     * @brief 按点编码批量采集并填充结果。
     * @param[in] point_codes 待处理的测点编码列表。
     * @param[out] results 批量处理结果列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    virtual int read_batch(const std::vector<std::string>& point_codes,
                           std::vector<PointValue>& results);

    /**
     * @brief 判断设备连接状态。
     * @return true 表示条件成立；false 表示不成立。
     */
    virtual bool is_connected() const { return false; }

    /**
     * @brief 返回最近的协议错误文本。
     * @return 最近一次错误的说明文本。
     */
    virtual std::string last_error() const { return {}; }

   protected:
    /** @brief 构造派生协议处理器。 */
    ProtocolHandler() = default;

    std::unordered_map<std::string, ValueType> config_;
    std::shared_ptr<MessageCapture> messageCapture_;
    std::atomic<bool> running_{false};
};

/** @brief 协议服务端处理器接口。 */
class ServerHandler : public ProtocolHandler {
   public:
    /** @brief 销毁协议服务端处理器。 */
    virtual ~ServerHandler() = default;

    /**
     * @brief 按功能码、从站和地址读取寄存器值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @return 得到的协议测点值。
     */
    virtual ValueType getValueByAddress(int funcCode, int slaveId, int address) = 0;

    /**
     * @brief 按功能码、从站和地址写入寄存器值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] value 待写入的值。
     */
    virtual void setValueByAddress(int funcCode, int slaveId, int address,
                                   const ValueType& value) = 0;

    /**
     * @copydoc ProtocolHandler::start
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> start() = 0;

    /**
     * @copydoc ProtocolHandler::stop
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> stop() = 0;

    /**
     * @copydoc ProtocolHandler::readValue
     * @param[in] point 测点数据。
     * @return 得到的协议测点值。
     */
    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;

    /**
     * @copydoc ProtocolHandler::writeValue
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point,
                            const ValueType& value) = 0;

    /**
     * @copydoc ProtocolHandler::addPoints
     * @param[in] points 测点数据列表。
     */
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;

    /**
     * @copydoc ProtocolHandler::initialize
     * @param[in] config 配置数据。
     */
    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;
};

/** @brief 协议客户端处理器接口。 */
class ClientHandler : public ProtocolHandler {
   public:
    /** @brief 销毁协议客户端处理器。 */
    virtual ~ClientHandler() = default;

    /**
     * @brief 建立设备连接。
     * @return true 表示操作成功；false 表示失败。
     */
    virtual bool connect() = 0;

    /** @brief 断开设备连接。 */
    virtual void disconnect() = 0;

    /**
     * @brief 判断设备是否已连接。
     * @return true 表示条件成立；false 表示不成立。
     */
    virtual bool isConnected() const = 0;

    /**
     * @copydoc ProtocolHandler::start
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> start() = 0;

    /**
     * @copydoc ProtocolHandler::stop
     * @return 用于获取异步操作结果的 future。
     */
    virtual std::future<bool> stop() = 0;

    /**
     * @copydoc ProtocolHandler::readValue
     * @param[in] point 测点数据。
     * @return 得到的协议测点值。
     */
    virtual ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) = 0;

    /**
     * @copydoc ProtocolHandler::writeValue
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    virtual bool writeValue(const std::shared_ptr<ProtocolPoint>& point,
                            const ValueType& value) = 0;

    /**
     * @copydoc ProtocolHandler::addPoints
     * @param[in] points 测点数据列表。
     */
    virtual void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) = 0;

    /**
     * @copydoc ProtocolHandler::initialize
     * @param[in] config 配置数据。
     */
    virtual void initialize(const std::unordered_map<std::string, ValueType>& config) = 0;
};

}  // namespace device
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_PROTOCOL_HANDLER_BASE_H
