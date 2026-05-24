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

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "net/protocol/common/protocol_handler.h"
#include "models/base.h"
#include "modbus_client.h"

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
    int         slaveId{1};       // 从站地址
    int         funcCode{3};      // Modbus 功能码 (1-4, 5, 6, 15, 16)
    int         address{0};       // 起始地址
    int         regCount{1};      // 寄存器数量（由 DecodeCode 自动推断）
    int         bitOffset{-1};    // 位偏移 (-1 表示非位操作)
    DecodeCode  decode{DecodeCode::UINT16};  // 数据类型
    EndianCode  endian{EndianCode::BigEndian};  // 端序
    std::string originalCode;     // 原始测点编码
};

/**
 * @brief 合并后的读取区间
 */
struct MergedRange {
    int    slaveId{1};
    int    funcCode{3};
    int    startAddr{0};
    int    count{0};
    int    pointStartIdx{0};
    int    pointCount{0};
};

/**
 * @brief Modbus 协议处理器
 */
class ModbusProtoHandler : public protocol::ProtocolHandler {
public:
    ModbusProtoHandler();
    ~ModbusProtoHandler() override;

    // ---- ProtocolHandler 接口实现 ----
    bool initialize(const json& config) override;
    bool connect() override;
    bool disconnect() override;
    bool is_connected() const override;
    int  read_batch(const std::vector<std::string>& point_codes,
                    std::vector<protocol::PointValue>& results) override;
    bool write_value(const std::string& point_code, double value) override;
    std::string last_error() const override;
    std::string protocol_name() const override;

private:
    // ---- 测点解析 ----
    static ParsedPoint parse_point_code(const std::string& code);

    // ---- 读取优化 ----
    std::vector<MergedRange> merge_ranges(const std::vector<ParsedPoint>& points) const;

    // ---- 值解码（统一入口） ----
    static protocol::PointValue read_point(const ParsedPoint& pp,
                                            const uint16_t* rawData, int rawLen);

    // ---- 值编码（写入时用） ----
    static bool encode_value(double value, const ParsedPoint& pp,
                              uint16_t* destRegs, int destLen);

    // ---- 功能码分组读取 ----
    ModbusClient::ErrorCode read_coils(const MergedRange& range, uint16_t* dest);
    ModbusClient::ErrorCode read_discrete_inputs(const MergedRange& range, uint16_t* dest);
    ModbusClient::ErrorCode read_holding_registers(const MergedRange& range, uint16_t* dest);
    ModbusClient::ErrorCode read_input_registers(const MergedRange& range, uint16_t* dest);

    // ---- 字节序组装/解析（核心端序引擎） ----
    /**
     * @brief 从 N 个 16 位寄存器中按端序组装 N*2 字节的原始缓冲区
     * @param regs    寄存器数组
     * @param regCount 寄存器数量
     * @param endian  端序
     * @param out     [out] 输出缓冲区 (regCount*2 字节)
     */
    static void assemble_bytes(const uint16_t* regs, int regCount,
                                EndianCode endian, uint8_t* out);

    /**
     * @brief 将值按端序写入寄存器数组
     */
    static void disassemble_bytes(const uint8_t* data, int byteCount,
                                   EndianCode endian, uint16_t* outRegs);

    // ---- 辅助 ----
    uint64_t now_ms() const;
    void     set_last_error(const std::string& err);

    std::unique_ptr<ModbusClient> client_;
    json                          config_;
    int                           response_timeout_ms_{2000};
    int                           slave_id_{1};

    mutable std::mutex            mutex_;
    std::string                   last_err_;
    std::string                   protocol_name_;
};

} // namespace modbus
} // namespace net
} // namespace ems

#endif // EMS_NET_MODBUS_PROTO_HANDLER_H
