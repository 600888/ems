/** @file client.hpp
 * @brief 自动管理传输、登录等待及应用关联的同步客户机。
 */
#pragma once
#include <dlt698/app/connection.hpp>
#include <dlt698/export.hpp>
#include <dlt698/service/sync.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>

namespace dlt698::app {
enum class ClientState { disconnected, connecting, connected, disconnecting };

struct ClientOptions {
    session::SessionOptions protocol;  ///< 地址、能力及请求超时；角色/登录/预设/心跳由场景控制。
    std::chrono::milliseconds transport_timeout{5000};  ///< DNS 与 TCP 建连的合计时限。
    std::chrono::milliseconds login_timeout{5000};      ///< remote_public 等待服务器 LINK 的时限。
    transport::ChannelOptions channel;                  ///< TCP 与简单串口的资源预算。
    std::function<void(const Error&)> diagnostic;       ///< 工作线程回调，异常被隔离，不得阻塞。
    session::TrafficHandler traffic;  ///< 可选收发观察器；工作线程借用事件，不得阻塞，异常被隔离。
};

/** @brief 托管同步客户机，每次连接拥有一个运行线程，调用方无需驱动事件循环。
 * @note 多业务线程可调用；同一连接只允许一个在途请求，冲突返回 busy，不排无界队列。
 * 不得与析构并发使用成员。回调不得强引用本客户机形成引用环；回调内同步调用返回 busy。
 * SET/ACTION 不自动重试，超时或断线时远端是否已执行可能未知。
 */
class Client {
   public:
    /** @brief 创建尚未连接的客户机。
     * @param[in] options 协议配置、阶段超时、资源预算及可选诊断。
     * @throws std::invalid_argument 阶段超时不是有效的正时长。
     */
    DLT698_API explicit Client(ClientOptions options = {});
    /** @brief 关闭传输并回收线程；自身回调内析构由托管收尾线程回收，不 detach。 */
    DLT698_API ~Client();
    /** @brief 禁止复制客户机身份及连接。 */
    Client(const Client&) = delete;
    /** @brief 禁止复制赋值客户机。 */
    Client& operator=(const Client&) = delete;
    /** @brief 建立 TCP，按场景等待 LINK 并完成公共 CONNECT。
     * @param[in] host 非空 IP 或 DNS 主机名。
     * @param[in] port 非零远端端口。
     * @param[in] profile 默认与托管服务器匹配的远程公共场景。
     * @return 已关联或配置、阶段超时、I/O、取消错误；CONNECT 拒绝返回 association_failed，
     * remote_code 保存原始拒绝码。失败自动收尾，可重试；不自动重新拨号。
     */
    DLT698_API Result<void> connect_tcp(
        std::string host, std::uint16_t port,
        ConnectionProfile profile = ConnectionProfile::remote_public);
    /** @brief 用默认 8E1 和串行链路适配打开串口并建立关联。
     * @param[in] path Windows COM 名或 POSIX 设备路径。
     * @param[in] baud 波特率，默认 9600。
     * @param[in] profile 默认普通本地 CONNECT。
     * @return 已关联或串口、配置、超时错误；操作系统同步打开耗时不受 transport_timeout 控制。
     */
    DLT698_API Result<void> open_serial(
        std::string path, unsigned baud = 9600,
        ConnectionProfile profile = ConnectionProfile::local_public);
    /** @brief 使用完整字格式与硬件 hooks 打开串口并建立关联。
     * @param[in] path 串口路径。
     * @param[in] serial 字格式、流控及通道预算。
     * @param[in] link 时序预算、真实排空及方向 hooks；速率/位数从 serial 派生。
     * @param[in] profile 关联场景。
     * @return 已关联或配置/I/O/超时错误；拒绝 1.5 停止位及无真实排空的手动方向配置。
     */
    DLT698_API Result<void> open_serial(
        std::string path, transport::SerialOptions serial, transport::SerialLinkOptions link,
        ConnectionProfile profile = ConnectionProfile::local_public);
    /** @brief 同步读取属性或一级元素。
     * @param[in] attribute 精确 OAD。
     * @return 拥有内存的 Data、原始 DAR 或本地错误，自动完成 GET Next 及链路分帧。
     */
    DLT698_API Result<service::ObjectValue> get(model::Oad attribute);
    /** @brief 同步读取非空属性列表，保留顺序及部分成功。
     * @param[in] attributes 精确 OAD 列表。
     * @return 逐项 Data/DAR 的完整响应或本地错误。
     */
    DLT698_API Result<protocol::apdu::GetResponse> get_list(std::vector<model::Oad> attributes);
    /** @brief 同步读取记录。
     * @param[in] record 完整记录 OAD、行选择和列选择。
     * @return 完整行列快照、DAR 或本地错误。
     */
    DLT698_API Result<protocol::apdu::RecordResult> get_record(protocol::apdu::GetRecord record);
    /** @brief 同步读取非空记录列表。
     * @param[in] records 有序查询列表。
     * @return 逐查询结果或本地错误。
     */
    DLT698_API Result<protocol::apdu::GetRecordResponse> get_record_list(
        std::vector<protocol::apdu::GetRecord> records);
    /** @brief 同步远端 SET，不重试。
     * @param[in] attribute 精确 OAD。
     * @param[in] value 精确 Data，不隐式转换倍率或单位。
     * @return 原始 DAR 或本地错误；外层成功不代表 DAR 为零。
     */
    DLT698_API Result<std::uint8_t> set(model::Oad attribute, model::Data value);
    /** @brief 同步远端 SET 列表，不回滚部分成功。
     * @param[in] attributes 非空属性值列表。
     * @return 逐项 DAR 响应或本地错误。
     */
    DLT698_API Result<protocol::apdu::SetResponse> set_list(
        std::vector<protocol::apdu::SetAttribute> attributes);
    /** @brief 同步远端 ACTION，不重试。
     * @param[in] method 精确 OMD。
     * @param[in] parameter 方法参数。
     * @return DAR/可选 Data 或本地错误。
     */
    DLT698_API Result<service::ActionValue> action(model::Omd method, model::Data parameter);
    /** @brief 同步远端 ACTION 列表。
     * @param[in] methods 非空方法/参数列表。
     * @return 逐项响应或本地错误。
     */
    DLT698_API Result<protocol::apdu::ActionResponse> action_list(
        std::vector<protocol::apdu::ActionMethod> methods);
    /** @brief 异步关闭连接并取消建连或在途请求，可从诊断回调调用。
     * @note 幂等；不发送 RELEASE，也不等待；SET/ACTION 取消不能保证远端未执行。
     */
    DLT698_API void request_disconnect();
    /** @brief 断开并等待所有托管 I/O 和线程收尾，随后可重新连接。
     * @return 成功或 RELEASE 错误；无论释放是否成功都关闭；自身回调或并发启停返回 busy。
     * @note 空闲关联先尝试 RELEASE；有在途请求时直接关闭并结束等待。重复断开成功。
     */
    DLT698_API Result<void> disconnect();
    /** @brief 查询最近发布的连接状态。
     * @return connected 表示协议关联完成；远端关闭最终变为 disconnected。
     */
    DLT698_API ClientState state() const noexcept;

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::app
