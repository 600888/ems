/** @file server.hpp
 * @brief 自动管理运行线程、协议连接和设备数据的 TCP/串口服务器。
 */
#pragma once
#include <dlt698/app/connection.hpp>
#include <dlt698/export.hpp>
#include <dlt698/service/device.hpp>
#include <dlt698/session/session.hpp>
#include <dlt698/transport/serial.hpp>
#include <dlt698/transport/serial_link.hpp>

namespace dlt698::app {
enum class ServerState { stopped, starting, running, stopping };

struct ServerOptions {
    session::SessionOptions
        protocol;  ///< 地址、实际能力、超时及限制；角色/登录/预设/心跳由入口场景控制。
    std::size_t max_connections = 16;     ///< TCP 活动连接上限；串口只有一个会话。
    std::uint16_t heartbeat_seconds = 5;  ///< remote_public 登录后的周期心跳，零关闭。
    transport::ChannelOptions channel;  ///< TCP 及简单串口的通道预算；完整重载使用 serial.channel。
    std::function<void(std::uint64_t, const Error&)> diagnostic;  ///< 连接标识及错误；0 为监听器。
    /// 可选收发观察器；非零连接 ID 在一次运行内唯一，重启后重新编号，与 diagnostic 共用 ID。
    /// 工作线程借用事件和字节，不得阻塞或强引用服务器；异常被隔离，保存数据须复制。
    std::function<void(std::uint64_t, const session::TrafficEvent&)> traffic;
};

/** @brief 托管服务器，默认持有空设备，可在启动前后直接设置数据。
 * @note 每次运行拥有独立 I/O 运行时和一个工作线程；停止保留设备数据。
 * 诊断回调在工作线程执行，异常被隔离，不得阻塞该线程或强引用本服务器形成环。
 * 不支持与析构并发调用成员；多个普通成员可从不同业务线程调用。
 */
class Server {
   public:
    /** @brief 创建拥有默认空设备的服务器。
     * @param[in] options 协议配置、连接上限及可选诊断回调。
     * @throws std::invalid_argument 连接上限为零。
     */
    DLT698_API explicit Server(ServerOptions options = {});
    /** @brief 创建使用共享设备的服务器，可与其他服务器共享数据。
     * @param[in] device 非空共享设备；连接关闭不释放应用持有的设备。
     * @param[in] options 协议配置、连接预算及诊断回调。
     * @throws std::invalid_argument 设备为空或连接上限为零。
     */
    DLT698_API Server(std::shared_ptr<service::Device> device, ServerOptions options = {});
    /** @brief 请求关闭并等待线程结束；在自身回调内销毁时由受管理的收尾线程负责回收。
     * @note 不抛异常，不丢弃取消回调；用户回调不得无限阻塞。
     */
    DLT698_API ~Server();
    /** @brief 禁止复制服务器身份和线程。 */
    Server(const Server&) = delete;
    /** @brief 禁止复制赋值服务器。 */
    Server& operator=(const Server&) = delete;
    /** @brief 本地设置完整属性，运行中首次发布或更新均可。
     * @param[in] attribute 特征零、索引零的 OAD。
     * @param[in] value 精确协议值，标准数组须符合设备布局。
     * @return Device 的校验结果；失败不改变已发布数据。
     */
    DLT698_API Result<void> set(model::Oad attribute, model::Data value);
    /** @brief 查询可继续配置或共享的设备。
     * @return 与服务器共同持有数据的共享设备，不暴露内部可变目录。
     */
    DLT698_API std::shared_ptr<service::Device> device() const;
    /** @brief 监听 TCP 并自动接入独立的协议服务器会话。
     * @param[in] address IPv4/IPv6 数字监听地址，不执行域名解析。
     * @param[in] port 监听端口，零由系统分配，成功后 local_port 可查询。
     * @param[in] profile 远程登录、普通本地关联或显式预设关联。
     * @return 本地监听就绪或配置/I/O 错误；已有运行或并发启停返回 busy。
     * @note 成功不代表已有客户端；remote_public 自动 LINK 登录，随后等待 CONNECT。
     */
    DLT698_API Result<void> start_tcp(std::string address, std::uint16_t port,
                                      ConnectionProfile profile = ConnectionProfile::remote_public);
    /** @brief 用默认 8E1 字格式打开串口并自动安装串行链路适配。
     * @param[in] path Windows COM 名称或 POSIX 设备路径。
     * @param[in] baud 波特率，默认 9600。
     * @param[in] profile 默认普通本地 CONNECT，无远程登录与心跳。
     * @return 接收入口就绪或串口/配置错误；失败释放部分打开的资源。
     */
    DLT698_API Result<void> start_serial(
        std::string path, unsigned baud = 9600,
        ConnectionProfile profile = ConnectionProfile::local_public);
    /** @brief 使用完整串口及排空/方向配置启动服务。
     * @param[in] path 串口设备路径。
     * @param[in] serial 实际速率、字格式、流控和原始通道预算。
     * @param[in] link 帧间预算及可选硬件 hooks；速率和位数由 serial 自动派生。
     * @param[in] profile 协议关联场景。
     * @return 就绪或配置/I/O 错误；手动方向切换缺真实排空回调时失败。
     */
    DLT698_API Result<void> start_serial(
        std::string path, transport::SerialOptions serial, transport::SerialLinkOptions link,
        ConnectionProfile profile = ConnectionProfile::local_public);
    /** @brief 幂等地请求停止接入并关闭会话，允许在诊断回调内调用。
     * @note 不同步等待，state 最终变为 stopped；业务线程可再调用 stop 等待收尾。
     */
    DLT698_API void request_stop();
    /** @brief 幂等停止并等待所有托管 I/O 工作及线程结束，保留设备数据。
     * @return 成功；自身运行回调内或并发启停时返回 busy，应使用 request_stop。
     * @note 通过在途 I/O 耗尽判断关闭，不用固定延时；之后可重新启动。
     */
    DLT698_API Result<void> stop();
    /** @brief 查询最近发布的服务器运行状态。
     * @return running 表示传输入口运行，不表示某个会话已经关联。
     */
    DLT698_API ServerState state() const noexcept;
    /** @brief 查询本次 TCP 监听端口。
     * @return 实际端口；串口或停止后返回零。
     */
    DLT698_API std::uint16_t local_port() const noexcept;
    /** @brief 查询当前活动协议会话数。
     * @return 最近发布的计数，关闭回调执行后释放名额。
     */
    DLT698_API std::size_t connections() const noexcept;

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::app
