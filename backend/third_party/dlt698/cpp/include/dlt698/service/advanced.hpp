/** @file advanced.hpp
 * @brief MD5、延时 ThenGet 与代理路由的服务器后端。
 */
#pragma once
#include <dlt698/service/service.hpp>

namespace dlt698::service {
class DLT698_API IProxyProvider {
   public:
    using Handler = session::Session::ExchangeHandler;
    using Cancel = std::function<void()>;
    /** @brief 初始化代理提供者，目标连接由实现管理。 */
    IProxyProvider();
    /** @brief 释放代理提供者，进行中任务的资源由实现自行管理。 */
    virtual ~IProxyProvider();
    /** @brief 在目标设备执行一个普通 GET/记录/SET/ACTION/ThenGet 请求。
     * @param[in] server 单地址 TSA，拥有内存。
     * @param[in] request 拥有型请求，不含外层 PROXY；目标会话分配自己的 PIID。
     * @param[in] handler 可从任意线程恰好完成一次，必须返回对应服务或错误。
     * @return 可选取消函数；取消必须非阻塞且可重复调用，不能保证远端副作用撤销。
     * @note 调用本身不得阻塞会话执行器；超时由 AdvancedService 控制。
     */
    virtual Cancel async_request(model::Tsa server, protocol::apdu::Apdu request,
                                 Handler handler) = 0;
};

/** @brief 按精确 TSA 路由到应用绑定的 Session；并发绑定与查找安全。 */
class ProxyRouter final : public IProxyProvider {
   public:
    /** @brief 创建空路由表。 */
    DLT698_API ProxyRouter();
    /** @brief 释放路由；已提交任务仍由目标 Session 管理。 */
    DLT698_API ~ProxyRouter() override;
    /** @brief 禁止复制路由器。 */
    ProxyRouter(const ProxyRouter&) = delete;
    /** @brief 禁止复制赋值路由器。 */
    ProxyRouter& operator=(const ProxyRouter&) = delete;
    /** @brief 绑定目标设备；相同 TSA 替换路由，不关闭原会话。
     * @param[in] server 单地址 TSA，须含描述字节及 1 至 16 个地址字节。
     * @param[in] target 非空、由应用完成关联的客户机会话；路由持有其生命周期。
     * @return 成功或 invalid_value；应用负责设备对象能力和安全配置。
     */
    DLT698_API Result<void> bind(model::Tsa server, std::shared_ptr<session::Session> target);
    /** @brief 查找并提交目标服务；未知地址异步错误由代理映射为逐项 DAR。
     * @param[in] server 精确 TSA。
     * @param[in] request 普通请求或 ThenGet。
     * @param[in] handler 目标 Session 执行器内完成一次。
     * @return 取消函数；调用将关闭目标会话以隔离迟到响应。
     * @note 同一目标最多一个在途事务；其他占用时返回 busy，不隐式排队有副作用请求。
     */
    DLT698_API Cancel async_request(model::Tsa server, protocol::apdu::Apdu request,
                                    Handler handler) override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct AdvancedServiceOptions {
    std::uint8_t default_read_delay_seconds = 0;
    std::uint16_t default_proxy_timeout_seconds = 5;  ///< 目标超时零值的替代，必须非零。
    Limits limits;
    std::shared_ptr<IProxyProvider> proxy;
    using TransHandler = std::function<IProxyProvider::Cancel(
        protocol::apdu::ProxyTransRequest,
        std::function<void(Result<protocol::apdu::ProxyTransResponse>)>)>;
    TransHandler trans;  ///< 透明端口后端；须非阻塞，完成/取消约定与 IProxyProvider 相同。
};

/** @brief 将 MD5、ThenGet 与 PROXY 接入现有对象目录和异步执行器。 */
class AdvancedService {
   public:
    /** @brief 安装异步高级请求处理器，替换 Session 原处理器。
     * @param[in] session 非空服务器会话，由应用管理连接。
     * @param[in] objects 非空对象目录，读写权限及方法参数校验沿用其 schema。
     * @param[in] executor 与 Session 相同的串行执行器。
     * @param[in] options 默认延时、代理后端、透明转发和资源上限。
     * @throws std::invalid_argument 参数为空或资源配置非法。
     * @note 一次只执行一个高级服务；ThenGet 按项执行写/方法→定时器→读，不回滚或重试。
     * provider 不得阻塞执行器；Session 关闭/释放后不再发起后续副作用。
     * 可与 ServerService 同时使用；对象目录与选项被处理器持有，不借用本对象地址。
     */
    DLT698_API AdvancedService(std::shared_ptr<session::Session> session,
                               std::shared_ptr<ObjectRegistry> objects,
                               std::shared_ptr<IExecutor> executor,
                               AdvancedServiceOptions options = {});
};
}  // namespace dlt698::service
