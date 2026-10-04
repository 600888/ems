/** @file modbus_protocol.h
 * @brief Modbus TCP/RTU PIMPL API；本头不依赖 libmodbus、JSON 库或平台系统头。
 */
#ifndef EMS_NET_PROTOCOL_MODBUS_PROTOCOL_H
#define EMS_NET_PROTOCOL_MODBUS_PROTOCOL_H

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {

/** @brief Modbus 客户端及只读上送服务端；每个实例独占自己的通信资源。
 * 生命周期与 I/O 由调用方串行调用；插件 C 桥内部提供串行保护。 */
class ModbusProtocol final {
   public:
    /** @brief 创建未配置实例，不建立连接或启动线程。 */
    ModbusProtocol();
    /** @brief 停止通信和工作线程，在创建实例的模块内释放 Impl。 */
    ~ModbusProtocol();
    /** @brief 禁止复制资源所有者。 @param[in] other 源实例。 */
    ModbusProtocol(const ModbusProtocol& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    ModbusProtocol& operator=(const ModbusProtocol& other) = delete;
    /** @brief 禁止移动活动实例。 @param[in] other 源实例。 */
    ModbusProtocol(ModbusProtocol&& other) = delete;
    /**
     * @brief 禁止移动赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    ModbusProtocol& operator=(ModbusProtocol&& other) = delete;

    /**
     * @brief 校验并保存 UTF-8 JSON 配置；配置失败不打开通信资源。
     * @param[in] json_config TCP/RTU、角色、端点、超时和服务端地址空间配置。
     * @return 0 成功；-1 配置非法或当前实例已连接。
     */
    int initialize(const std::string& json_config);
    /** @brief 建立连接或启动只读监听。 @return 0 成功；-1 失败并保存诊断。 */
    int connect();
    /** @brief 停止通信，等待内部线程退出。 @return 0 成功；-1 停止失败。 */
    int disconnect();
    /** @brief 查询连接或监听状态。 @return true 表示通信资源已启动。 */
    bool is_connected() const;
    /**
     * @brief 批量读点，保持输入顺序并输出带品质和 UTC 毫秒时标的 JSON 数组。
     * @param[in] point_codes slave:function:offset[:decode[:endian[:bit]]] 编码列表。
     * @param[out] result_json 本次结果文本，由调用方对象持有。
     * @return 成功点数；-1 表示参数或连接错误；失败点保留无效品质。
     */
    int read_batch(const std::vector<std::string>& point_codes, std::string& result_json);
    /**
     * @brief 客户端执行已授权点写；服务端仅更新本地出口映像。
     * @param[in] point_code 点地址、数据类型和字序编码；只读客户端地址禁止写入。
     * @param[in] value 写值；整数须精确且在范围内，浮点须有限。
     * @return 0 成功；-1 编码非法、越界或通信失败。
     */
    int write(const std::string& point_code, double value);
    /** @brief 返回脱离 Impl 生命周期的诊断副本。 @return 最近一次错误。 */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::net::protocol
#endif
