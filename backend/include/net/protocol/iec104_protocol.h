/** @file iec104_protocol.h
 * @brief IEC 60870-5-104 PIMPL API；公开头不依赖协议栈、JSON 或系统平台头。
 */
#ifndef EMS_NET_PROTOCOL_IEC104_PROTOCOL_H
#define EMS_NET_PROTOCOL_IEC104_PROTOCOL_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "net/protocol/protocol_plugin_api.h"

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
     * @brief 带绝对期限的客户端控制；选择与执行共用该期限。
     * @param[in] point_code CA:IOA:ASDU 编码；必须是控制类型。
     * @param[in] value 有限且符合该 ASDU 类型范围的值。
     * @param[in] expires_at_ms UTC 毫秒绝对期限；已过期时拒发且不改变会话。
     * @return 0 协议确认成功；-1 拒绝、期限已过或结果未知。
     * @note 实际等待不超过命令期限与通道 command_timeout_ms 的较小值。
     */
    int write_until(const std::string& point_code, double value, std::int64_t expires_at_ms);
    /**
     * @brief 取出服务端已受理、尚未交给宿主的北向控制请求。
     * @param[out] result_json JSON 数组，元素含 id、point_id、code、value、expires_at_ms。
     * @return 本次交付的请求数；0 表示无待处理；-1 未初始化或非服务端角色。
     * @note 每个请求只交付一次；宿主须用 resolve_control 回执，超时后自动收敛。
     */
    int take_controls(std::string& result_json);
    /**
     * @brief 回执北向控制执行结果，并向上级发送结束确认。
     * @param[in] control_id take_controls 返回的请求 ID。
     * @param[in] accepted 是否执行成功。
     * @param[in] reason 失败原因，可空。
     * @return 0 已发送或会话已关闭；-1 未知 ID 或非服务端角色。
     */
    int resolve_control(std::int64_t control_id, bool accepted, const std::string& reason);
    /**
     * @brief 客户端提交总召；服务端不支持此操作。
     * @param[in] common_address 公共地址；0 表示分别向所有配置站提交总召。
     * @return 0 已提交；-1 未就绪、地址非法或发送失败；不表示总召已经完成。
     */
    int interrogate(int common_address = 0);
    /** @brief 返回诊断副本。 @return 最近一次错误文本。 */
    std::string last_error() const;

    /**
     * @brief 安装或注销收发字节观察器；注销会等待在途回调结束。
     * @param[in] callback 回调，空表示关闭观察；不得重入插件。
     * @param[in,out] user 宿主上下文，注销返回后可释放。
     */
    void set_traffic_handler(ems_plugin_traffic_callback callback, void* user);

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ems::net::protocol
#endif
