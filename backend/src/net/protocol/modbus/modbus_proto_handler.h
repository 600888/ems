/**
 * @file modbus_proto_handler.h
 * @brief Modbus 协议插件处理器
 *
 * 实现 ems::net::protocol::ProtocolHandler 抽象接口，
 * 封装 ModbusClient 提供 TCP/RTU 统一的数据采集能力。
 * 编译为 libproto_modbus.so，通过 dlopen 动态加载。
 *
 * 测点地址格式: "slave_id:func_code:address[:decode_code[:endian_code]]"
 *   - slave_id:    从站地址 (1-255)
 *   - func_code:   功能码 (1=Coil, 2=DiscreteInput, 3=HoldingRegister, 4=InputRegister)
 *   - address:     寄存器/线圈地址
 *   - decode_code: 解码方式 (0=UINT16, 1=INT16, 2=UINT32, 3=INT32, 4=FLOAT, 5=DOUBLE, ...)
 *   - endian_code: 端序 (0=BIG_ENDIAN, 1=LITTLE_ENDIAN, 2=BIG_ENDIAN_BYTE_SWAP, 3=LITTLE_ENDIAN_BYTE_SWAP)
 *
 * 示例: "1:3:0:4:0" (从站1, 功能码3, 地址0, FLOAT, 大端序)
 *       默认 decode_code=0 (UINT16), endian_code=0 (BigEndian)
 */
#ifndef EMS_NET_MODBUS_PROTO_HANDLER_H
#define EMS_NET_MODBUS_PROTO_HANDLER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "modbus_client.h"
#include "models/base.h"
#include "net/protocol/common/protocol_handler.h"

namespace ems {
namespace net {
namespace modbus {

using json = nlohmann::json;
using model::DecodeCode;
using model::EndianCode;

/**
 * @brief 解析后的测点信息
 *
 * 将字符串格式的测点编码解析为结构化字段，
 * 数据类型 (DecodeCode) 和端序 (EndianCode) 分离存储。
 */
struct ParsedPoint {
    int slaveId{1};                            // 从站地址
    int funcCode{3};                           // Modbus 功能码 (1-4, 5, 6, 15, 16)
    int address{0};                            // 起始地址
    int regCount{1};                           // 寄存器数量（由 DecodeCode 自动推断）
    int bitOffset{-1};                         // 位偏移 (-1 表示非位操作)
    DecodeCode decode{DecodeCode::UINT16};     // 数据类型
    EndianCode endian{EndianCode::BigEndian};  // 端序
    std::string originalCode;                  // 原始测点编码
};

/**
 * @brief 合并后的读取区间
 */
struct MergedRange {
    int slaveId{1};
    int funcCode{3};
    int startAddr{0};
    int count{0};
    int pointStartIdx{0};
    int pointCount{0};
};

/**
 * @brief Modbus 协议处理器
 */
class ModbusProtoHandler : public protocol::ProtocolHandler {
   public:
    /** @brief 构造 ModbusProtoHandler 实例。 */
    ModbusProtoHandler();
    /** @brief 释放 ModbusProtoHandler 持有的资源。 */
    ~ModbusProtoHandler() override;

    // ---- ProtocolHandler 接口实现 ----
    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool initialize(const json& config) override;
    /**
     * @brief 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connect() override;
    /**
     * @brief 断开连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool disconnect() override;
    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_connected() const override;
    /**
     * @brief 批量读取指定测点的当前值。
     * @param[in] point_codes 待处理的测点编码列表。
     * @param[out] results 批量处理结果列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    int read_batch(const std::vector<std::string>& point_codes,
                   std::vector<protocol::PointValue>& results) override;
    /**
     * @brief 将数值写入指定测点。
     * @param[in] point_code 测点编码。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool write_value(const std::string& point_code, double value) override;
    /**
     * @brief 获取最近一次错误说明。
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const override;
    /**
     * @brief 获取协议名称。
     * @return 协议名称。
     */
    std::string protocol_name() const override;

   private:
    // ---- 测点解析 ----
    /**
     * @brief 解析测点编码中的 Modbus 地址及类型。
     * @param[in] code 对象编码。
     * @return 得到的解析后的测点信息。
     */
    static ParsedPoint parse_point_code(const std::string& code);

    // ---- 读取优化 ----
    /**
     * @brief 将相邻测点合并为批量读取区间。
     * @param[in] points 测点数据列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<MergedRange> merge_ranges(const std::vector<ParsedPoint>& points) const;

    // ---- 值解码（统一入口） ----
    /**
     * @brief 从原始寄存器数据解码测点值。
     * @param[in] pp 解析后的测点配置。
     * @param[in] rawData 原始寄存器数据。
     * @param[in] rawLen 原始数据长度。
     * @return 得到的测点值。
     */
    static protocol::PointValue read_point(const ParsedPoint& pp, const uint16_t* rawData,
                                           int rawLen);

    // ---- 值编码（写入时用） ----
    /**
     * @brief 将工程值编码为待写入的寄存器数据。
     * @param[in] value 输入值。
     * @param[in] pp 解析后的测点配置。
     * @param[out] destRegs 目标寄存器缓冲区。
     * @param[in] destLen 目标缓冲区的寄存器容量。
     * @return true 表示操作成功；false 表示失败。
     */
    static bool encode_value(double value, const ParsedPoint& pp, uint16_t* destRegs, int destLen);

    // ---- 功能码分组读取 ----
    /**
     * @brief 批量读取线圈状态。
     * @param[in] range 合并后的读取地址区间。
     * @param[out] dest 目标数据缓冲区。
     * @return Modbus 操作结果码。
     */
    ModbusClient::ErrorCode read_coils(const MergedRange& range, uint16_t* dest);
    /**
     * @brief 批量读取离散输入状态。
     * @param[in] range 合并后的读取地址区间。
     * @param[out] dest 目标数据缓冲区。
     * @return Modbus 操作结果码。
     */
    ModbusClient::ErrorCode read_discrete_inputs(const MergedRange& range, uint16_t* dest);
    /**
     * @brief 批量读取保持寄存器。
     * @param[in] range 合并后的读取地址区间。
     * @param[out] dest 目标数据缓冲区。
     * @return Modbus 操作结果码。
     */
    ModbusClient::ErrorCode read_holding_registers(const MergedRange& range, uint16_t* dest);
    /**
     * @brief 批量读取输入寄存器。
     * @param[in] range 合并后的读取地址区间。
     * @param[out] dest 目标数据缓冲区。
     * @return Modbus 操作结果码。
     */
    ModbusClient::ErrorCode read_input_registers(const MergedRange& range, uint16_t* dest);

    // ---- 字节序组装/解析（核心端序引擎） ----
    /**
     * @brief 从 N 个 16 位寄存器中按端序组装 N*2 字节的原始缓冲区
     * @param[in] regs    寄存器数组
     * @param[in] regCount 寄存器数量
     * @param[in] endian  端序
     * @param[out] out     [out] 输出缓冲区 (regCount*2 字节)
     */
    static void assemble_bytes(const uint16_t* regs, int regCount, EndianCode endian, uint8_t* out);

    /**
     * @brief 将值按端序写入寄存器数组
     * @param[in] data 测点数据。
     * @param[in] byteCount 字节数量。
     * @param[in] endian Modbus 寄存器字节序。
     * @param[out] outRegs 接收编码结果的寄存器缓冲区。
     */
    static void disassemble_bytes(const uint8_t* data, int byteCount, EndianCode endian,
                                  uint16_t* outRegs);

    // ---- 辅助 ----
    /**
     * @brief 获取当前毫秒时间戳。
     * @return 计算得到的数值。
     */
    uint64_t now_ms() const;
    /**
     * @brief 保存最近一次错误说明。
     * @param[in] err 错误信息。
     */
    void set_last_error(const std::string& err);

    std::unique_ptr<ModbusClient> client_;
    json config_;
    int response_timeout_ms_{2000};
    int slave_id_{1};

    mutable std::mutex mutex_;
    std::string last_err_;
    std::string protocol_name_;
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_PROTO_HANDLER_H
