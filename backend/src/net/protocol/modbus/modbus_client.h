/**
 * @file modbus_client.h
 * @brief Modbus 客户端（Master）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_CLIENT_H
#define EMS_NET_MODBUS_CLIENT_H

#include <modbuspp/data.h>
#include <modbuspp/master.h>
#include <modbuspp/slave.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ems {
namespace net {
namespace modbus {

class ModbusClient {
   public:
    enum class NetType { RTU = 0, TCP = 1 };

    enum class ErrorCode {
        Success = 0,
        ConnectionFailed = -1,
        Timeout = -2,
        InvalidAddress = -3,
        InvalidData = -4,
        SlaveError = -5,
        Unknown = -99
    };

    using ReadCallback = std::function<void(const uint16_t* data, int count)>;
    using ErrorCallback = std::function<void(ErrorCode, const std::string&)>;

    /**
     * @brief 构造 ModbusClient 实例。
     * @param[in] type 连接或数据类型。
     * @param[in] connection 连接地址或串口参数。
     * @param[in] settings 连接附加设置。
     */
    ModbusClient(NetType type, const std::string& connection, const std::string& settings);
    /** @brief 释放 ModbusClient 持有的资源。 */
    ~ModbusClient();

    /**
     * @brief 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connect();
    /** @brief 断开连接。 */
    void disconnect();
    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const;

    /**
     * @brief 设置 Modbus 响应超时时间，单位为毫秒。
     * @param[in] ms 时间长度，单位为毫秒。
     */
    void setResponseTimeout(int ms);
    /**
     * @brief 设置 Modbus 字节间超时时间，单位为毫秒。
     * @param[in] ms 时间长度，单位为毫秒。
     */
    void setByteTimeout(int ms);
    /**
     * @brief 启用或关闭 Modbus 调试输出。
     * @param[in] enable 是否启用该功能。
     */
    void setDebug(bool enable);

    /**
     * @brief 从指定从站读取线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readCoils(int slaveId, int addr, bool* dest, int nb = 1);
    /**
     * @brief 从指定从站读取离散输入。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readDiscreteInputs(int slaveId, int addr, bool* dest, int nb = 1);
    /**
     * @brief 从指定从站读取保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readHoldingRegisters(int slaveId, int addr, uint16_t* dest, int nb = 1);
    /**
     * @brief 从指定从站读取输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readInputRegisters(int slaveId, int addr, uint16_t* dest, int nb = 1);

    /**
     * @brief 向指定从站写入单个线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeSingleCoil(int slaveId, int addr, bool value);
    /**
     * @brief 向指定从站写入单个保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeSingleRegister(int slaveId, int addr, uint16_t value);
    /**
     * @brief 向指定从站写入多个线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] src 源数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeMultipleCoils(int slaveId, int addr, const bool* src, int nb);
    /**
     * @brief 向指定从站写入多个保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] src 源数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeMultipleRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    /**
     * @brief 一次事务中写入并读取保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] writeAddr 写入区间起点。
     * @param[in] src 源数据缓冲区。
     * @param[in] writeNb 写入寄存器数量。
     * @param[in] readAddr 读取区间起点。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] readNb 读取寄存器数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeReadRegisters(int slaveId, int writeAddr, const uint16_t* src, int writeNb,
                                 int readAddr, uint16_t* dest, int readNb);

    /**
     * @brief 注册指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return true 表示操作成功；false 表示失败。
     */
    bool addSlave(int slaveId);
    /**
     * @brief 移除指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return true 表示操作成功；false 表示失败。
     */
    bool removeSlave(int slaveId);
    /**
     * @brief 检查指定从站是否已注册。
     * @param[in] slaveId Modbus 从站编号。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool hasSlave(int slaveId) const;

    /**
     * @brief 从从站读取数据并按指定字节序解码。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] data 测点数据。
     * @return Modbus 操作结果码。
     */
    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readData(int slaveId, int addr, Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    /**
     * @brief 按指定字节序将数据写入从站。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] data 测点数据。
     * @return Modbus 操作结果码。
     */
    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeData(int slaveId, int addr, const Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        return writeMultipleRegisters(slaveId, addr, data.registers().data(), regCount);
    }

    /**
     * @brief 获取最近一次错误代码或说明。
     * @return 查询得到的数值。
     */
    int getLastError() const { return lastError_; }

    /**
     * @brief 获取最近一次错误说明。
     * @return 最近一次错误的说明文本。
     */
    std::string getLastErrorMsg() const { return lastErrorMsg_; }

   private:
    /**
     * @brief 将 Modbus 库返回值转换为客户端错误代码。
     * @param[in] modbusRet Modbus 库的原始返回码。
     * @return Modbus 操作结果码。
     */
    ErrorCode translateError(int modbusRet);
    /**
     * @brief 查找指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return 匹配的对象指针；未找到时为空。
     */
    Modbus::Slave* getSlave(int slaveId);

    NetType netType_;
    Modbus::Master master_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::unique_ptr<Modbus::Slave>> slaves_;
    int lastError_{0};
    std::string lastErrorMsg_;
    bool connected_{false};
};

template <Modbus::Endian E = Modbus::EndianBig>
class ModbusClientWithData {
   public:
    /**
     * @brief 构造 ModbusClientWithData 实例。
     * @param[in] type 连接或数据类型。
     * @param[in] connection 连接地址或串口参数。
     * @param[in] settings 连接附加设置。
     * @param[in] slaveId Modbus 从站编号。
     */
    ModbusClientWithData(typename ModbusClient::NetType type, const std::string& connection,
                         const std::string& settings, int slaveId = 1)
        : client_(type, connection, settings), slave_id_(slaveId) {}

    /**
     * @brief 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connect() { return client_.connect(); }

    /** @brief 断开连接。 */
    void disconnect() { client_.disconnect(); }

    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const { return client_.isConnected(); }

    /**
     * @brief 设置当前从站编号。
     * @param[in] slaveId Modbus 从站编号。
     */
    void setSlaveId(int slaveId) { slave_id_ = slaveId; }

    /**
     * @brief 获取当前从站编号。
     * @return 查询得到的数值。
     */
    int getSlaveId() const { return slave_id_; }

    /**
     * @brief 读取Int16 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readInt16(int addr, int16_t& value) {
        uint16_t raw;
        auto ret = client_.readHoldingRegisters(slave_id_, addr, &raw, 1);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = static_cast<int16_t>(raw);
        }
        return ret;
    }

    /**
     * @brief 读取UInt16 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readUInt16(int addr, uint16_t& value) {
        return client_.readHoldingRegisters(slave_id_, addr, &value, 1);
    }

    /**
     * @brief 读取Int32 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readInt32(int addr, int32_t& value) {
        Modbus::Data<int32_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取UInt32 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readUInt32(int addr, uint32_t& value) {
        Modbus::Data<uint32_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取Float 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readFloat(int addr, float& value) {
        Modbus::Data<float, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取Double 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readDouble(int addr, double& value) {
        Modbus::Data<double, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取Int64 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode readInt64(int addr, int64_t& value) {
        Modbus::Data<int64_t, E> data;
        auto ret = client_.readData(slave_id_, addr, data);
        if (ret == ModbusClient::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 写入Int16 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeInt16(int addr, int16_t value) {
        return client_.writeSingleRegister(slave_id_, addr, static_cast<uint16_t>(value));
    }

    /**
     * @brief 写入UInt16 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeUInt16(int addr, uint16_t value) {
        return client_.writeSingleRegister(slave_id_, addr, value);
    }

    /**
     * @brief 写入Int32 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeInt32(int addr, int32_t value) {
        Modbus::Data<int32_t, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    /**
     * @brief 写入UInt32 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeUInt32(int addr, uint32_t value) {
        Modbus::Data<uint32_t, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    /**
     * @brief 写入Float 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeFloat(int addr, float value) {
        Modbus::Data<float, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    /**
     * @brief 写入Double 类型的寄存器数据。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusClient::ErrorCode writeDouble(int addr, double value) {
        Modbus::Data<double, E> data(value);
        return client_.writeData(slave_id_, addr, data);
    }

    /**
     * @brief 获取底层 Modbus 客户端。
     * @return 对应对象的引用。
     */
    ModbusClient& client() { return client_; }

   private:
    ModbusClient client_;
    int slave_id_;
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_CLIENT_H
