/**
 * @file modbus_handler.h
 * @brief Modbus 协议处理器（设备级 + 采集级统一 | 服务端 + 客户端 + 批量读取 + 端序引擎）
 *
 * 存量设备处理器保留 libmodbuspp 调用；新采集路径使用同目录的
 * ModbusProtoHandler，通过运行时加载器调用协议插件。
 *
 * 测点格式: "slave_id:func_code:address[:decode_code[:endian_code]]"
 *   decode_code: 0=UINT16, 1=INT16, 2=UINT32, 3=INT32, 4=FLOAT, 5=DOUBLE, ...
 *   endian_code: 0=BigEndian, 1=LittleEndian, 2=BigEndianByteSwap, 3=LittleEndianByteSwap
 */
#ifndef EMS_NET_HANDLER_MODBUS_LEGACY_H
#define EMS_NET_HANDLER_MODBUS_LEGACY_H

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

#include "models/base.h"
#include "net/handler/base_handler.h"
#include "net/protocol/modbus/modbus_client.h"
#include "net/protocol/modbus/modbus_limits.h"
#include "net/protocol/modbus/modbus_server.h"
#include "net/protocol/serial_types.h"
#include "net/protocol/time_units.h"

namespace ems {
namespace net {
namespace device {
using modbus::Defaults;
using modbus::FunctionCode;
using modbus::Layout;
using modbus::Limits;
using protocol::SerialDefaults;
using protocol::TimeUnits;

using json = nlohmann::json;
using model::DecodeCode;
using model::EndianCode;

// ============================================================
// 辅助类型
// ============================================================

class CoilBits {
   public:
    /** @brief 构造 CoilBits 实例。 */
    CoilBits() = default;

    /**
     * @brief 构造 CoilBits 实例。
     * @param[in] count 数据项数量。
     */
    explicit CoilBits(size_t count)
        : data_(count > 0 ? (count + enum_value(Layout::BitsPerByte) - 1) /
                                enum_value(Layout::BitsPerByte)
                          : 0),
          count_(count) {}

    /**
     * @brief 调整线圈位数组长度。
     * @param[in] count 数据项数量。
     */
    void resize(size_t count) {
        count_ = count;
        data_.resize(count > 0 ? (count + enum_value(Layout::BitsPerByte) - 1) /
                                     enum_value(Layout::BitsPerByte)
                               : 0);
    }

    /**
     * @brief 获取线圈位数。
     * @return 处理或统计得到的数据项数量。
     */
    size_t size() const { return count_; }

    /**
     * @brief 读取指定位置的线圈状态。
     * @param[in] index 数据项索引。
     * @return true 表示操作成功；false 表示失败。
     */
    bool operator[](size_t index) const {
        return (index < count_) && ((data_[index / enum_value(Layout::BitsPerByte)] &
                                     (1 << (index % enum_value(Layout::BitsPerByte)))) != 0);
    }

    /**
     * @brief 设置指定位置的线圈状态。
     * @param[in] index 数据项索引。
     * @param[in] value 待写入的值。
     */
    void set(size_t index, bool value) {
        if (index >= count_) return;
        if (value)
            data_[index / enum_value(Layout::BitsPerByte)] |=
                (1 << (index % enum_value(Layout::BitsPerByte)));
        else
            data_[index / enum_value(Layout::BitsPerByte)] &=
                ~(1 << (index % enum_value(Layout::BitsPerByte)));
    }

    /**
     * @brief 获取压缩线圈缓冲区的可写视图。
     * @return 指向对象内部数据缓冲区的指针。
     */
    bool* data() { return reinterpret_cast<bool*>(data_.data()); }

    /**
     * @brief 获取压缩线圈缓冲区的只读视图。
     * @return 指向对象内部数据缓冲区的指针。
     */
    const bool* data() const { return reinterpret_cast<const bool*>(data_.data()); }

   private:
    std::vector<uint8_t> data_;
    size_t count_ = 0;
};

class ModbusWriteCallback {
   public:
    /** @brief 释放 ModbusWriteCallback 持有的资源。 */
    virtual ~ModbusWriteCallback() = default;
    /**
     * @brief 接收客户端写入通知。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] address Modbus 地址。
     * @param[in] values 待读取或写入的数值列表。
     */
    virtual void onModbusClientWrite(int slaveId, int funcCode, int address,
                                     const std::vector<int>& values) = 0;
};

// ============================================================
// 插件级测点解析
// ============================================================

struct ParsedPoint {
    int slaveId{1};
    int funcCode{enum_value(FunctionCode::ReadHoldingRegisters)};
    int address{0};
    int regCount{1};
    int bitOffset{-1};
    DecodeCode decode{DecodeCode::UINT16};
    EndianCode endian{EndianCode::BigEndian};
    std::string originalCode;
};

struct MergedRange {
    int slaveId{1}, funcCode{enum_value(FunctionCode::ReadHoldingRegisters)}, startAddr{0},
        count{0}, pointStartIdx{0}, pointCount{0};
};

// ============================================================
// ModbusServerHandler
// ============================================================

class ModbusServerHandler : public ServerHandler {
   public:
    using Logger = std::function<void(const std::string&)>;
    /** @brief 构造 ModbusServerHandler 实例。 */
    ModbusServerHandler();
    /**
     * @brief 构造 ModbusServerHandler 实例。
     * @param[in] logger 日志回调。
     */
    explicit ModbusServerHandler(Logger logger);
    /** @brief 释放 ModbusServerHandler 持有的资源。 */
    ~ModbusServerHandler() override;

    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     */
    void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    /**
     * @brief 启动服务。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> start() override;
    /**
     * @brief 停止服务。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> stop() override;
    /**
     * @brief 读取指定测点的当前值。
     * @param[in] point 测点数据。
     * @return 得到的协议测点值。
     */
    ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) override;
    /**
     * @brief 将值写入指定测点。
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) override;
    /**
     * @brief 注册协议测点列表。
     * @param[in] points 测点数据列表。
     */
    void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) override;
    /**
     * @brief 按功能码、站号和地址查询测点值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @return 得到的协议测点值。
     */
    ValueType getValueByAddress(int funcCode, int slaveId, int address) override;
    /**
     * @brief 按功能码、站号和地址写入测点值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] value 待写入的值。
     */
    void setValueByAddress(int funcCode, int slaveId, int address, const ValueType& value) override;
    /**
     * @brief 按解码格式查询指定地址的测点值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] decode 测点数据解码格式。
     * @return 得到的协议测点值。
     */
    ValueType getValueByAddressWithDecode(int funcCode, int slaveId, int address,
                                          const std::string& decode);
    /**
     * @brief 按编码格式写入指定地址的测点值。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] value 待写入的值。
     * @param[in] decode 测点数据解码格式。
     */
    void setValueByAddressWithDecode(int funcCode, int slaveId, int address, const ValueType& value,
                                     const std::string& decode);

    /**
     * @brief 获取底层 Modbus 服务端。
     * @return 对应对象的指针。
     */
    std::shared_ptr<modbus::ModbusServer> server() const { return server_; }

    /**
     * @brief 获取最近捕获的协议报文。
     * @param[in] limit 结果数量上限。
     * @return 匹配条件的数据集合。
     */
    std::vector<CapturedMessage> getCapturedMessages(
        int limit = enum_value(Defaults::CaptureMessageLimit)) const override;
    /** @brief 清空已捕获的协议报文。 */
    void clearCapturedMessages() override;
    /**
     * @brief 获取协议请求的平均耗时统计。
     * @return 得到的时间。
     */
    AvgTimeStats getAvgTime() const override;
    /**
     * @brief 设置服务端写入通知回调。
     * @param[in] callback 操作完成或变化时调用的回调。
     */
    void setWriteCallback(std::shared_ptr<ModbusWriteCallback> callback);

    /**
     * @brief 获取底层 Modbus 服务端。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<modbus::ModbusServer> getServer() const { return server_; }

   private:
    /**
     * @brief 处理服务端接收到的写入操作。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] address Modbus 地址。
     * @param[in] values 待读取或写入的数值列表。
     */
    void onModbusWriteInternal(int slaveId, int funcCode, int address,
                               const std::vector<int>& values);
    /**
     * @brief 将配置中的地址文本解析为寄存器地址。
     * @param[in] s 待解析的文本。
     * @return 计算得到的数值。
     */
    int parseAddress(const std::string& s) const;
    /**
     * @brief 获取指定解码类型需要的寄存器数。
     * @param[in] decode 测点数据解码格式。
     * @return 处理或统计得到的数据项数量。
     */
    int getRegisterCount(const std::string& decode) const;
    /**
     * @brief 将通用值转换为整数。
     * @param[in] v 待设置的数值。
     * @param[out] r 结果对象。
     * @return true 表示操作成功；false 表示失败。
     */
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
    int port_{enum_value(Defaults::TcpPort)};
};

// ============================================================
// ModbusClientHandler (设备级 + 插件级合并)
// ============================================================

class ModbusClientHandler : public ClientHandler {
   public:
    using Logger = std::function<void(const std::string&)>;
    /** @brief 构造 ModbusClientHandler 实例。 */
    ModbusClientHandler();
    /**
     * @brief 构造 ModbusClientHandler 实例。
     * @param[in] logger 日志回调。
     */
    explicit ModbusClientHandler(Logger logger);
    /** @brief 释放 ModbusClientHandler 持有的资源。 */
    ~ModbusClientHandler() override;

    // ---- 设备级接口 ----
    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     */
    void initialize(const std::unordered_map<std::string, ValueType>& config) override;
    /**
     * @brief 启动服务。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> start() override;
    /**
     * @brief 停止服务。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> stop() override;
    /**
     * @brief 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connect() override;
    /** @brief 断开连接。 */
    void disconnect() override;
    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const override;
    /**
     * @brief 读取指定测点的当前值。
     * @param[in] point 测点数据。
     * @return 得到的协议测点值。
     */
    ValueType readValue(const std::shared_ptr<ProtocolPoint>& point) override;
    /**
     * @brief 将值写入指定测点。
     * @param[in] point 测点数据。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeValue(const std::shared_ptr<ProtocolPoint>& point, const ValueType& value) override;
    /**
     * @brief 注册协议测点列表。
     * @param[in] points 测点数据列表。
     */
    void addPoints(const std::vector<std::shared_ptr<ProtocolPoint>>& points) override;
    /**
     * @brief 获取最近捕获的协议报文。
     * @param[in] limit 结果数量上限。
     * @return 匹配条件的数据集合。
     */
    std::vector<CapturedMessage> getCapturedMessages(
        int limit = enum_value(Defaults::CaptureMessageLimit)) const override;
    /** @brief 清空已捕获的协议报文。 */
    void clearCapturedMessages() override;
    /**
     * @brief 获取协议请求的平均耗时统计。
     * @return 得到的时间。
     */
    AvgTimeStats getAvgTime() const override;

    // ---- 批量读取 ----
    /**
     * @brief 批量读取连续的 Modbus 寄存器。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] count 数据项数量。
     * @return 匹配条件的数据集合。
     */
    std::vector<int> readRegistersBatchAsync(int funcCode, int slaveId, int startAddress,
                                             int count);
    /**
     * @brief 从指定从站读取保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] count 数据项数量。
     * @return 匹配条件的数据集合。
     */
    std::vector<int> readHoldingRegisters(int slaveId, int startAddress, int count);
    /**
     * @brief 从指定从站读取输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] count 数据项数量。
     * @return 匹配条件的数据集合。
     */
    std::vector<int> readInputRegisters(int slaveId, int startAddress, int count);
    /**
     * @brief 从指定从站读取线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] count 数据项数量。
     * @return 得到的线圈位数组。
     */
    CoilBits readCoils(int slaveId, int startAddress, int count);
    /**
     * @brief 从指定从站读取离散输入。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] count 数据项数量。
     * @return 得到的线圈位数组。
     */
    CoilBits readDiscreteInputs(int slaveId, int startAddress, int count);
    /**
     * @brief 向指定从站写入单个保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeSingleRegister(int slaveId, int address, int value);
    /**
     * @brief 向指定从站写入多个保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] values 待读取或写入的数值列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeMultipleRegisters(int slaveId, int startAddress, const std::vector<int>& values);
    /**
     * @brief 向指定从站写入单个线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeSingleCoil(int slaveId, int address, bool value);
    /**
     * @brief 向指定从站写入多个线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddress 地址区间起点。
     * @param[in] values 待读取或写入的数值列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool writeMultipleCoils(int slaveId, int startAddress, const CoilBits& values);

    /**
     * @brief 获取底层 Modbus 客户端。
     * @return 对应对象的指针。
     */
    std::shared_ptr<modbus::ModbusClient> client() const { return client_; }

    // ---- 插件级接口 ----
    /**
     * @brief 批量读取指定测点的当前值。
     * @param[in] point_codes 待处理的测点编码列表。
     * @param[out] results 批量处理结果列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    int read_batch(const std::vector<std::string>& point_codes,
                   std::vector<ProtocolHandler::PointValue>& results) override;

    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_connected() const override { return isConnected(); }

    /**
     * @brief 获取最近一次错误说明。
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const override { return last_err_; }

    // ---- 测点解析（插件级，供 modbus_plugin.cpp 复用） ----
    /**
     * @brief 解析测点编码中的 Modbus 地址及类型。
     * @param[in] code 对象编码。
     * @return 得到的解析后的测点信息。
     */
    static ParsedPoint parse_point_code(const std::string& code);

   private:
    /**
     * @brief 根据功能码、地址和解码方式读取测点。
     * @param[in] funcCode Modbus 功能码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] address Modbus 地址。
     * @param[in] decode 测点数据解码格式。
     * @return 得到的协议测点值。
     */
    ValueType readValueInternal(int funcCode, int slaveId, int address, const std::string& decode);
    /**
     * @brief 从整数中提取指定位置的位值。
     * @param[in] value 输入值。
     * @param[in] bit_offset 位偏移量。
     * @return 计算得到的数值。
     */
    int extractBitValue(int value, int bit_offset) const;
    /**
     * @brief 更新整数中指定位置的位值。
     * @param[in] current_value 当前整数值。
     * @param[in] bit_offset 位偏移量。
     * @param[in] bit_value 需要设置的位值。
     * @return 计算得到的数值。
     */
    int applyBitValue(int current_value, int bit_offset, bool bit_value) const;
    /**
     * @brief 将配置中的地址文本解析为寄存器地址。
     * @param[in] s 待解析的文本。
     * @return 计算得到的数值。
     */
    int parseAddress(const std::string& s) const;
    /**
     * @brief 获取指定解码类型需要的寄存器数。
     * @param[in] decode 测点数据解码格式。
     * @return 处理或统计得到的数据项数量。
     */
    int getRegisterCount(const std::string& decode) const;
    /**
     * @brief 将通用值转换为整数。
     * @param[in] v 待设置的数值。
     * @param[out] r 结果对象。
     * @return true 表示操作成功；false 表示失败。
     */
    bool valueToInt(const ValueType& v, int& r) const;

    // ---- 插件级 ----
    /**
     * @brief 将相邻测点合并为批量读取区间。
     * @param[in] points 测点数据列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<MergedRange> merge_ranges(const std::vector<ParsedPoint>& points) const;
    /**
     * @brief 按指定字节序将寄存器组合为字节序列。
     * @param[in] regs 寄存器数据。
     * @param[in] regCount 寄存器数量。
     * @param[in] endian Modbus 寄存器字节序。
     * @param[out] out 结果缓冲区。
     */
    static void assemble_bytes(const uint16_t* regs, int regCount, EndianCode endian, uint8_t* out);
    /**
     * @brief 按指定字节序将字节序列写回寄存器。
     * @param[in] data 测点数据。
     * @param[in] byteCount 字节数量。
     * @param[in] endian Modbus 寄存器字节序。
     * @param[out] outRegs 接收编码结果的寄存器缓冲区。
     */
    static void disassemble_bytes(const uint8_t* data, int byteCount, EndianCode endian,
                                  uint16_t* outRegs);
    /**
     * @brief 从原始寄存器数据解码测点值。
     * @param[in] pp 解析后的测点配置。
     * @param[in] rawData 原始寄存器数据。
     * @param[in] rawLen 原始数据长度。
     * @return 得到的测点值。
     */
    ProtocolHandler::PointValue read_point(const ParsedPoint& pp, const uint16_t* rawData,
                                           int rawLen);
    /**
     * @brief 读取一个合并后的 Modbus 地址区间。
     * @param[in] range 合并后的读取地址区间。
     * @param[out] buf 数据缓冲区。
     * @param[in] bufCount bufCount 的数量。
     * @return Modbus 操作结果码。
     */
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
    int defaultSlaveId_{1}, responseTimeoutMs_{enum_value(Defaults::ResponseTimeoutMs)},
        byteTimeoutMs_{enum_value(Defaults::ByteTimeoutMs)};
    mutable std::string last_err_;
};

}  // namespace device
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_HANDLER_MODBUS_LEGACY_H
