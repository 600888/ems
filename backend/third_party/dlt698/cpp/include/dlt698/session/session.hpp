/** @file session.hpp
 * @brief 单个协议对端的连接、事务、持续接收和单调超时管理。
 */
#pragma once
#include <dlt698/common/executor.hpp>
#include <dlt698/export.hpp>
#include <dlt698/protocol/apdu/apdu.hpp>
#include <dlt698/protocol/link/fragment.hpp>
#include <dlt698/protocol/link/frame.hpp>
#include <dlt698/security/backend.hpp>
#include <dlt698/session/traffic.hpp>
#include <dlt698/transport/channel.hpp>

namespace dlt698::session {
enum class Role { client, server };
enum class State { disconnected, preconnected, associating, associated, releasing, closed };

struct SessionOptions {
    Role role = Role::client;
    protocol::link::ServerAddress server;
    std::uint8_t client_address = 0;
    Limits limits;
    protocol::apdu::AssociationParameters parameters{
        0x0010, {0xff, 0xff, 0xf8}, {}, 1024, 1024, 1, 1024, 100};
    // function 默认全零；应用须显式配置自身实际支持的 C.2 业务位，CONNECT 取双方交集。
    protocol::apdu::FactoryVersion factory;
    std::shared_ptr<security::IBackend>
        security_backend;  ///< 会话独占实例，生命周期覆盖所有安全调用。
    std::function<std::shared_ptr<security::IBackend>()>
        security_backend_factory;  ///< 每次构造会话调用，必须返回非空新实例；不能同时指定 security_backend。
    bool protect_application = true;  ///< 有后端时，关联后的业务必须使用 SECURITY，拒绝明文降级。
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds id_reuse_delay{
        120000};                      ///< 异常事务和 REPORT 的序号隔离期，须覆盖对端最大响应寿命。
    bool require_login = false;       ///< 远程场景可要求先由协议服务器发起 LINK 登录。
    bool preset_association = false;  ///< 显式启用本地通道的预设连接，使用本地能力和限制。
    bool clock_trusted = false;
    std::uint16_t heartbeat_seconds =
        0;  ///< 服务器登录成功后自动心跳周期，零为关闭；失败关闭通道。
    std::optional<model::Ti> request_time_tag;  ///< 客户机请求自动添加时间标签；响应须原样回传。
    std::chrono::milliseconds fragment_timeout{1000};    ///< 单片确认的单调超时。
    std::chrono::milliseconds reassembly_timeout{5000};  ///< 重组没有进展时的单调超时。
    unsigned fragment_retries = 2;  ///< 0 至 16 次，仅重发未获确认的相同片段，不重放应用请求。
    unsigned report_retries = 2;    ///< 上报未确认时重发次数，0 至 16；超时最终关闭通道。
    bool prefer_get_blocks = true;  ///< 超长 GET 优先按完整属性/记录行应用分块，其块仍可链路分帧。
    std::function<model::DateTime()>
        calendar_clock;  ///< 空时使用 UTC；注入时钟在执行器中调用，抛异常会关闭会话并返回 invalid_value。
};

/** @brief 校验会话配置，不构造会话、不调用安全工厂或清理后端。
 * @param[in] options 地址、能力、时间和安全后端配置。
 * @return 成功或 invalid_value；工厂返回值及实例独占性在实际构造会话时检查。
 * @note 托管入口可在创建监听器或线程前调用，避免预校验消耗认证材料。
 */
DLT698_API Result<void> validate_options(const SessionOptions& options);

/**
 * @brief 一个通道上的会话，支持公共连接、读写/方法、记录及两类分段事务。
 * @note 所有状态和回调在提供的串行执行器上处理；TCP 拨号方向与协议角色独立。
 * 超时或取消关闭物理通道，以隔离线上没有 generation 字段的迟到响应。
 * 析构时排队任务不保活会话；已提交请求仍须驱动执行器才能收到 closed 回调。
 */
class Session {
   public:
    using ConnectHandler = std::function<void(Result<protocol::apdu::ConnectResponse>)>;
    using GetHandler = std::function<void(Result<protocol::apdu::GetResponse>)>;
    using SetHandler = std::function<void(Result<protocol::apdu::SetResponse>)>;
    using ActionHandler = std::function<void(Result<protocol::apdu::ActionResponse>)>;
    using SetRequestHandler =
        std::function<protocol::apdu::SetResponse(const protocol::apdu::SetRequest&)>;
    using ActionRequestHandler =
        std::function<protocol::apdu::ActionResponse(const protocol::apdu::ActionRequest&)>;
    using LinkHandler = std::function<void(Result<protocol::apdu::LinkResponse>)>;
    using ReleaseHandler = std::function<void(Result<void>)>;
    using RequestHandler =
        std::function<protocol::apdu::GetResponse(const protocol::apdu::GetRequest&)>;
    using RecordHandler = std::function<void(Result<protocol::apdu::GetRecordResponse>)>;
    using RecordRequestHandler =
        std::function<protocol::apdu::GetRecordResponse(const protocol::apdu::GetRecordRequest&)>;
    using DiagnosticHandler = std::function<void(const Error&)>;
    using CloseHandler = std::function<void(const Error&)>;
    using StateHandler = std::function<void(State)>;
    using ExchangeHandler = std::function<void(Result<protocol::apdu::Apdu>)>;
    using BackendCancel = std::function<void()>;
    using AdvancedRequestHandler =
        std::function<BackendCancel(protocol::apdu::Apdu, ExchangeHandler)>;
    using ReportHandler = std::function<bool(const protocol::apdu::ReportNotification&)>;
    using FollowHandler = std::function<void(const protocol::apdu::FollowReport&)>;
    /** @brief 提交 MD5、ThenGet、PROXY 请求或服务器 REPORT 通知。
     * @param[in] request 拥有内存的消息；PIID 与请求时间标签由会话分配。
     * @param[in] handler 在串行执行器内恰好完成一次，返回精确响应或本地错误。
     * @note 每个发起方最多一个在途事务；请求不重试，REPORT 按 report_retries 重发。
     * 超时或取消关闭通道；ThenGet/PROXY 超时不表示远端未执行。
     */
    DLT698_API void async_exchange(protocol::apdu::Apdu request, ExchangeHandler handler);
    /** @brief 注册高级请求的异步服务器后端。
     * @param[in] handler 接收拥有型请求及完成回调，不得阻塞同一执行器；空值移除。
     * @note 完成回调可从任意线程调用，但只接受首次结果；释放/关闭后结果被丢弃。
     * 处理器返回非阻塞取消函数，释放/关闭时调用；应弱引用会话并自行管理资源和超时。
     */
    DLT698_API void set_advanced_handler(AdvancedRequestHandler handler);
    /** @brief 注册客户机主动上报接收器。
     * @param[in] handler 执行器内调用，成功接收返回 true 才发送确认；false/异常不确认。
     * @note 没有处理器时不确认；确认过的完全相同重发在隔离期内重发确认，不重复交付。
     */
    DLT698_API void set_report_handler(ReportHandler handler);
    /** @brief 注册已匹配响应与合法通知的跟随上报观察器。
     * @param[in] handler 执行器内借用数据，需跨回调使用时复制；异常被隔离。
     */
    DLT698_API void set_follow_handler(FollowHandler handler);
    /** @brief 注册 ACD 请求访问通知；应用根据业务选择事件读取 OAD。
     * @param[in] handler 在合法响应/通知带 ACD 且已协商时调用；不得阻塞，异常被隔离。
     * @note ACD 不指定事件 OAD，因此不会隐式发起 GET；可投递后续业务读取。
     */
    DLT698_API void set_acd_handler(std::function<void()> handler);
    /** @brief 设置服务器待访问状态，后续服务器 APDU 按协商结果携带 ACD。
     * @param[in] pending true 有待处理事件，false 清除；仅影响服务器发送。
     */
    DLT698_API void set_access_demand(bool pending);
    /** @brief 创建尚未启动的会话。
     * @param[in] channel 已连接的字节通道。
     * @param[in] executor 所有会话操作及定时器共用的串行执行器。
     * @param[in] options 协议角色、精确地址、能力与超时配置。
     * @throws std::invalid_argument 通道/执行器为空、配置非法、工厂返回空值或后端已被其他存活会话持有。
     * @note 工厂在构造线程调用一次，异常向调用者传播；工厂不得返回其他会话使用中的实例。
     */
    DLT698_API Session(std::shared_ptr<transport::IChannel> channel,
                       std::shared_ptr<IExecutor> executor, SessionOptions options = {});
    /** @brief 关闭通道，未完成事务以 closed 结束；仍需驱动执行器交付回调。 */
    DLT698_API ~Session();
    /** @brief 禁止复制会话身份及事务。 */
    Session(const Session&) = delete;
    /** @brief 禁止复制赋值会话。 */
    Session& operator=(const Session&) = delete;
    /** @brief 启动持续接收泵；预设连接只在显式配置后启用，重复启动无额外效果。 */
    DLT698_API void start();
    /** @brief 注册服务器 GET 处理器。
     * @param[in] handler 在会话执行器内同步调用的处理器，不得阻塞等待同一执行器。
     */
    DLT698_API void set_request_handler(RequestHandler handler);
    /** @brief 注册记录查询处理器，返回拥有全部行数据的快照。
     * @param[in] handler 在串行执行器内调用，不得阻塞；缺省返回 DAR=4。
     * @note Session 缓存本次结果直到分块完成/超时，后续分页不再次调用 provider。
     */
    DLT698_API void set_record_handler(RecordRequestHandler handler);
    /** @brief 读取记录或记录列表，并自动收齐 GET Next 数据块。
     * @param[in] records 非空查询；每项保存完整 RSD/RCSD。
     * @param[in] list false 恰好一项，true 使用 RecordList。
     * @param[in] handler 返回有精确列类型的完整快照或错误。
     */
    DLT698_API void async_get_record(std::vector<protocol::apdu::GetRecord> records, bool list,
                                     RecordHandler handler);
    /** @brief 注册服务器 SET 处理器。
     * @param[in] handler 在执行器内同步执行，不得阻塞；未注册时逐项返回拒绝 DAR。
     */
    DLT698_API void set_set_handler(SetRequestHandler handler);
    /** @brief 注册服务器 ACTION 处理器。
     * @param[in] handler 在执行器内同步执行，不得阻塞；未注册时逐项返回拒绝 DAR。
     */
    DLT698_API void set_action_handler(ActionRequestHandler handler);
    /** @brief 注册不会抢占当前事务的诊断回调。
     * @param[in] handler 接收损坏帧、不匹配地址/方向、迟到响应等诊断。
     */
    DLT698_API void set_diagnostic_handler(DiagnosticHandler handler);
    /** @brief 注册通道收发观察器，不改变协议处理与事务匹配。
     * @param[in] handler 在会话执行器内借用事件和字节；空值移除，异常被隔离。
     * @note 注册异步生效，建议在 start 前调用；RX 使用处理时的观察器，TX 使用提交时的观察器。
     * 已提交的 TX 即使关闭、注销或销毁会话仍通知原观察器，须继续驱动执行器。
     * 写入成功不代表对端收到或处理；编码失败未提交通道时没有 TX 事件。
     * 不得阻塞或同步等待本执行器；跨回调保存数据须复制，不应强引用会话形成环。
     * 未注册时不为发送观察复制缓冲区；注册后每个在途写入额外保留一份完整帧。
     */
    DLT698_API void set_traffic_handler(TrafficHandler handler);
    /** @brief 由协议客户机发起公共 CONNECT。
     * @param[in] handler 返回协商响应或本地错误，远端拒绝原码保存在响应 result 中。
     */
    DLT698_API void async_connect(ConnectHandler handler);
    /** @brief 由协议服务器发起登录、单次心跳或退出登录。
     * @param[in] type 预连接请求类型。
     * @param[in] heartbeat_seconds 线上心跳周期（秒），自动周期另由 options 配置。
     * @param[in] handler 返回响应或错误。
     */
    DLT698_API void async_link(protocol::apdu::LinkRequestType type,
                               std::uint16_t heartbeat_seconds, LinkHandler handler);
    /** @brief 由已关联客户机读取属性，最多一个在途事务。
     * @param[in] attributes 非空的精确 OAD 列表。
     * @param[in] list false 时必须恰好一个属性，true 使用 NormalList。
     * @param[in] handler 返回按原顺序保留 Data/DAR 的响应或错误。
     * @note 自动收齐分块；TimeTag 由 options 配置，超时/取消关闭通道隔离迟到结果。
     */
    DLT698_API void async_get(std::vector<model::Oad> attributes, bool list, GetHandler handler);
    /** @brief 发起 SET 普通或列表事务，不自动重试或回滚部分成功。
     * @param[in] attributes 属性与精确 Data 值，非空。
     * @param[in] list false 时恰好一项，true 使用列表。
     * @param[in] handler 返回逐项 DAR 或本地错误，超时不能证明远端未执行。
     */
    DLT698_API void async_set(std::vector<protocol::apdu::SetAttribute> attributes, bool list,
                              SetHandler handler);
    /** @brief 发起 ACTION 普通或列表事务，不自动重试有副作用的方法。
     * @param[in] methods 精确 OMD 与参数 Data，非空。
     * @param[in] list false 时恰好一项，true 使用列表。
     * @param[in] handler 返回逐项 DAR/可选 Data 或本地错误，超时后远端执行结果未知。
     */
    DLT698_API void async_action(std::vector<protocol::apdu::ActionMethod> methods, bool list,
                                 ActionHandler handler);
    /** @brief 检查当前线程是否正在驱动本会话的执行环境。
     * @return 在执行器/所属 I/O 运行时的回调中为 true，此时不得同步等待同一环境。
     */
    DLT698_API bool in_executor_thread() const noexcept;
    /** @brief 结束在途事务的本地等待并发起应用连接释放，物理通道仍可继续使用。
     * @param[in] handler 释放完成或错误回调。
     * @note SET/ACTION 已发出或已进入通道队列时，释放不保证远端未执行。
     */
    DLT698_API void async_release(ReleaseHandler handler);
    /** @brief 取消在途事务并关闭物理通道，避免旧响应被后续事务匹配。 */
    DLT698_API void cancel();
    /** @brief 幂等关闭会话、通道和定时器，结束未完成事务。 */
    DLT698_API void close();
    /** @brief 设置会话关闭观察器，供连接所有者及时释放会话名额。
     * @param[in] handler 在会话执行器内调用，接收拥有者可复制的关闭原因；空值移除观察器。
     * @note 每次注册最多通知一次；已关闭时异步通知已保存的原因。
     * 状态先变为 closed 并结束在途事务，再通知；不表示物理通道取消回调已全部排空。
     * 回调不得同步等待本执行器，异常被隔离，不应强引用本会话形成引用环。
     */
    DLT698_API void set_close_handler(CloseHandler handler);
    /** @brief 注册状态变化观察器，供托管入口在 LINK 就绪后发起 CONNECT。
     * @param[in] handler 在会话执行器内接收新状态，空值移除；异常被隔离。
     * @note 注册任务执行时先通知当前状态，此后只通知实际变化。观察器不得阻塞或强引用会话。
     * closed 状态通知不代表事务已完成，应通过关闭观察器观察终结原因和事务完成顺序。
     */
    DLT698_API void set_state_handler(StateHandler handler);
    /** @brief 查询原子发布的会话状态。
     * @return 最近一次已执行的状态，投递但未执行的操作不立即改变状态。
     */
    DLT698_API State state() const noexcept;

   private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace dlt698::session
