/** @file iec104_protocol.h
 * @brief IEC 60870-5-104 PIMPL API；公开头不依赖协议栈、JSON 或系统平台头。
 */
#ifndef EMS_NET_PROTOCOL_IEC104_PROTOCOL_H
#define EMS_NET_PROTOCOL_IEC104_PROTOCOL_H

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {

/** @brief 每实例一个客户端连接或只读上送服务端；生命周期和 API 调用须串行。
 * 协议栈接收线程只更新内部缓存；C 插件桥提供 API 串行保护。 */
class Iec104Protocol final {
   public:
    /** @brief 创建未配置实例，不打开网络或启动线程。 */
    Iec104Protocol();
    /** @brief 停止并等待所有协议栈线程，再释放内部资源。 */
    ~Iec104Protocol();
    /** @brief 禁止复制资源所有者。 @param[in] other 源实例。 */
    Iec104Protocol(const Iec104Protocol& other) = delete;
    /** @brief 禁止复制赋值。 @param[in] other 源实例。 @return 不可调用。 */
    Iec104Protocol& operator=(const Iec104Protocol& other) = delete;
    /** @brief 禁止移动活动回调对象。 @param[in] other 源实例。 */
    Iec104Protocol(Iec104Protocol&& other) = delete;
    /** @brief 禁止移动赋值。 @param[in] other 源实例。 @return 不可调用。 */
    Iec104Protocol& operator=(Iec104Protocol&& other) = delete;

    /**
     * @brief 校验并保存 UTF-8 JSON 配置，支持旧通道 settings JSON 字符串。
     * @param[in] json_config 角色、端点、公共地址、APCI、超时和服务端测点映像。
     * @return 0 成功；-1 配置非法或资源尚未断开，诊断由 last_error 获取。
     */
    int initialize(const std::string& json_config);
    /** @brief 建立 TCP 并等待 STARTDT 确认，或启动服务端监听。
     * @return 0 数据传输就绪/监听成功；-1 失败，清理本次通信资源。 */
    int connect();
    /** @brief 停止并等待回调线程退出，清除客户端缓存。
     * @return 0 已完全停止；重复调用成功。 */
    int disconnect();
    /** @brief 查询客户端 STARTDT 就绪或服务端监听状态。
     * @return true 表示数据传输就绪或正在监听。 */
    bool is_connected() const;
    /**
     * @brief 读取接收缓存或本地出口映像，不发送逐点读请求；保持输入顺序。
     * @param[in] point_codes CA:IOA:ASDU 编码；ASDU 可为类型编号或标准标识符。
     * @param[out] result_json 带品质、UTC 毫秒时标和 IEC 品质的结果数组。
     * @return 品质有效的点数；-1 参数/连接错误；缺值、过期、类型不符输出 null。
     */
    int read_batch(const std::vector<std::string>& point_codes, std::string& result_json);
    /**
     * @brief 客户端执行遥控/遥调并等待协议确认；服务端更新已配置监视点并自发上送。
     * @param[in] point_code CA:IOA:ASDU 编码；客户端只接受控制类型。
     * @param[in] value 有限且符合该 ASDU 类型范围的值。
     * @return 0 协议确认成功/本地映像更新；-1 拒绝、通信错误或结果未知。
     * @note 确认不等于设备物理动作完成；超时断开会话且不自动重发命令。
     */
    int write(const std::string& point_code, double value);
    /**
     * @brief 客户端提交总召；服务端不支持此操作。
     * @param[in] common_address 公共地址；0 表示分别向所有配置站提交总召。
     * @return 0 已提交；-1 未就绪、地址非法或发送失败；不表示总召已经完成。
     */
    int interrogate(int common_address = 0);
    /** @brief 返回诊断副本。 @return 最近一次错误文本。 */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::net::protocol
#endif
