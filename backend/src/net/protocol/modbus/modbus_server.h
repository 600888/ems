/**
 * @file modbus_server.h
 * @brief Modbus 服务端（Server）实现
 * @author cdy
 * @date 2026-04-07
 */
#ifndef EMS_NET_MODBUS_SERVER_H
#define EMS_NET_MODBUS_SERVER_H

#include <modbuspp/bufferedslave.h>
#include <modbuspp/server.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ems {
namespace net {
namespace modbus {

class ModbusServer {
   public:
    enum class NetType { RTU = 0, TCP = 1 };

    enum class ErrorCode {
        Success = 0,
        BindFailed = -1,
        StartFailed = -2,
        AlreadyRunning = -3,
        Unknown = -99
    };

    using DataChangeCallback =
        std::function<void(int slaveId, Modbus::Table table, int addr, int nb)>;

    /**
     * @brief 构造 ModbusServer 实例。
     * @param[in] type 连接或数据类型。
     * @param[in] connection 连接地址或串口参数。
     * @param[in] settings 连接附加设置。
     */
    ModbusServer(NetType type, const std::string& connection, const std::string& settings);
    /** @brief 释放 ModbusServer 持有的资源。 */
    ~ModbusServer();

    /**
     * @brief 启动服务。
     * @return true 表示操作成功；false 表示失败。
     */
    bool start();
    /** @brief 停止服务。 */
    void stop();
    /**
     * @brief 检查服务是否正在运行。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isRunning() const;
    /** @brief 等待工作线程退出。 */
    void join();

    /**
     * @brief 注册指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return 计算得到的数值。
     */
    int addSlave(int slaveId);
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
     * @brief 配置从站的保持寄存器地址区间。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddr 地址区间起点。
     * @param[in] count 数据项数量。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setHoldingRegisterBlock(int slaveId, int startAddr, int count);
    /**
     * @brief 配置从站的输入寄存器地址区间。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddr 地址区间起点。
     * @param[in] count 数据项数量。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setInputRegisterBlock(int slaveId, int startAddr, int count);
    /**
     * @brief 配置从站的线圈地址区间。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddr 地址区间起点。
     * @param[in] count 数据项数量。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setCoilBlock(int slaveId, int startAddr, int count);
    /**
     * @brief 配置从站的离散输入地址区间。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddr 地址区间起点。
     * @param[in] count 数据项数量。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setDiscreteInputBlock(int slaveId, int startAddr, int count);

    /**
     * @brief 向指定从站写入保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeHoldingRegister(int slaveId, int addr, uint16_t value);
    /**
     * @brief 向指定从站写入保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] src 源数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeHoldingRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    /**
     * @brief 向指定从站写入输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeInputRegister(int slaveId, int addr, uint16_t value);
    /**
     * @brief 向指定从站写入输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] src 源数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeInputRegisters(int slaveId, int addr, const uint16_t* src, int nb);
    /**
     * @brief 向指定从站写入线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeCoil(int slaveId, int addr, bool value);
    /**
     * @brief 向指定从站写入线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] src 源数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode writeCoils(int slaveId, int addr, const bool* src, int nb);

    /**
     * @brief 从指定从站读取保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode readHoldingRegister(int slaveId, int addr, uint16_t& value);
    /**
     * @brief 从指定从站读取保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readHoldingRegisters(int slaveId, int addr, uint16_t* dest, int nb);
    /**
     * @brief 从指定从站读取输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode readInputRegister(int slaveId, int addr, uint16_t& value);
    /**
     * @brief 从指定从站读取输入寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readInputRegisters(int slaveId, int addr, uint16_t* dest, int nb);
    /**
     * @brief 从指定从站读取线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode readCoil(int slaveId, int addr, bool& value);
    /**
     * @brief 从指定从站读取线圈。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readCoils(int slaveId, int addr, bool* dest, int nb);
    /**
     * @brief 从指定从站读取离散输入。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    ErrorCode readDiscreteInput(int slaveId, int addr, bool& value);
    /**
     * @brief 从指定从站读取离散输入。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] dest 目标数据缓冲区。
     * @param[in] nb 寄存器或线圈数量。
     * @return Modbus 操作结果码。
     */
    ErrorCode readDiscreteInputs(int slaveId, int addr, bool* dest, int nb);

    /**
     * @brief 将结构化数据写入保持寄存器。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] data 测点数据。
     * @return Modbus 操作结果码。
     */
    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode writeHoldingData(int slaveId, int addr, const Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        return writeHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
    }

    /**
     * @brief 从保持寄存器读取结构化数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] data 测点数据。
     * @return Modbus 操作结果码。
     */
    template <typename T, Modbus::Endian E = Modbus::EndianBig>
    ErrorCode readHoldingData(int slaveId, int addr, Modbus::Data<T, E>& data) {
        int regCount = sizeof(T) / 2;
        auto ret = readHoldingRegisters(slaveId, addr, data.registers().data(), regCount);
        if (ret == ErrorCode::Success) {
            data.updateValue();
        }
        return ret;
    }

    /**
     * @brief 设置数据变化通知回调。
     * @param[in] cb 操作完成或变化时调用的回调。
     */
    void setDataChangeCallback(DataChangeCallback cb) { dataChangeCallback_ = cb; }

   private:
    /** @brief 运行服务端消息循环。 */
    void runLoop();
    /**
     * @brief 处理服务端收到的 Modbus 请求。
     * @param[in] req 收到的 Modbus 请求。
     * @param[in,out] dev 处理请求的 Modbus 设备。
     * @return 计算得到的数值。
     */
    static int messageHandler(Modbus::Message* req, Modbus::Device* dev);

    NetType netType_;
    Modbus::Server server_;
    std::string connection_;
    std::string settings_;
    std::map<int, std::shared_ptr<Modbus::BufferedSlave>> slaves_;
    std::unique_ptr<std::thread> serverThread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    DataChangeCallback dataChangeCallback_;
};

template <Modbus::Endian E = Modbus::EndianBig>
class ModbusServerWithData {
   public:
    /**
     * @brief 构造 ModbusServerWithData 实例。
     * @param[in] type 连接或数据类型。
     * @param[in] connection 连接地址或串口参数。
     * @param[in] settings 连接附加设置。
     */
    ModbusServerWithData(typename ModbusServer::NetType type, const std::string& connection,
                         const std::string& settings)
        : server_(type, connection, settings) {}

    /**
     * @brief 启动服务。
     * @return true 表示操作成功；false 表示失败。
     */
    bool start() { return server_.start(); }

    /** @brief 停止服务。 */
    void stop() { server_.stop(); }

    /**
     * @brief 检查服务是否正在运行。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isRunning() const { return server_.isRunning(); }

    /** @brief 等待工作线程退出。 */
    void join() { server_.join(); }

    /**
     * @brief 注册指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return 计算得到的数值。
     */
    int addSlave(int slaveId) { return server_.addSlave(slaveId); }

    /**
     * @brief 移除指定站号的 Modbus 从站。
     * @param[in] slaveId Modbus 从站编号。
     * @return true 表示操作成功；false 表示失败。
     */
    bool removeSlave(int slaveId) { return server_.removeSlave(slaveId); }

    /**
     * @brief 配置从站的保持寄存器地址区间。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] startAddr 地址区间起点。
     * @param[in] count 数据项数量。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setHoldingRegisterBlock(int slaveId, int startAddr, int count) {
        return server_.setHoldingRegisterBlock(slaveId, startAddr, count);
    }

    /**
     * @brief 写入Int16 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeInt16(int slaveId, int addr, int16_t value) {
        return server_.writeHoldingRegister(slaveId, addr, static_cast<uint16_t>(value));
    }

    /**
     * @brief 写入UInt16 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeUInt16(int slaveId, int addr, uint16_t value) {
        return server_.writeHoldingRegister(slaveId, addr, value);
    }

    /**
     * @brief 写入Int32 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeInt32(int slaveId, int addr, int32_t value) {
        Modbus::Data<int32_t, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    /**
     * @brief 写入UInt32 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeUInt32(int slaveId, int addr, uint32_t value) {
        Modbus::Data<uint32_t, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    /**
     * @brief 写入Float 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeFloat(int slaveId, int addr, float value) {
        Modbus::Data<float, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    /**
     * @brief 写入Double 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[in] value 待写入的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode writeDouble(int slaveId, int addr, double value) {
        Modbus::Data<double, E> data(value);
        return server_.writeHoldingData(slaveId, addr, data);
    }

    /**
     * @brief 读取Int16 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readInt16(int slaveId, int addr, int16_t& value) {
        uint16_t raw;
        auto ret = server_.readHoldingRegister(slaveId, addr, raw);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = static_cast<int16_t>(raw);
        }
        return ret;
    }

    /**
     * @brief 读取UInt16 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readUInt16(int slaveId, int addr, uint16_t& value) {
        return server_.readHoldingRegister(slaveId, addr, value);
    }

    /**
     * @brief 读取Int32 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readInt32(int slaveId, int addr, int32_t& value) {
        Modbus::Data<int32_t, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取UInt32 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readUInt32(int slaveId, int addr, uint32_t& value) {
        Modbus::Data<uint32_t, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取Float 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readFloat(int slaveId, int addr, float& value) {
        Modbus::Data<float, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 读取Double 类型的寄存器数据。
     * @param[in] slaveId Modbus 从站编号。
     * @param[in] addr Modbus 起始地址。
     * @param[out] value 接收读取结果的值。
     * @return Modbus 操作结果码。
     */
    typename ModbusServer::ErrorCode readDouble(int slaveId, int addr, double& value) {
        Modbus::Data<double, E> data;
        auto ret = server_.readHoldingData(slaveId, addr, data);
        if (ret == ModbusServer::ErrorCode::Success) {
            value = data.value();
        }
        return ret;
    }

    /**
     * @brief 获取底层 Modbus 服务端。
     * @return 对应对象的引用。
     */
    ModbusServer& server() { return server_; }

   private:
    ModbusServer server_;
};

}  // namespace modbus
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_MODBUS_SERVER_H
