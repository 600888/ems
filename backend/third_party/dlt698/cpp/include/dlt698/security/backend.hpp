/** @file backend.hpp
 * @brief ESAM 厂商 SDK 的协议安全适配合约；库不保存明文密钥。
 */
#pragma once
#include <dlt698/protocol/apdu/connection.hpp>
#include <dlt698/protocol/apdu/security.hpp>

namespace dlt698::security {
struct AuthenticationResult {
    std::uint8_t result = 255;  ///< CONNECT 标准结果码，默认拒绝。
    std::optional<protocol::apdu::SecurityData> security;
};

/** @brief 每个 Session 独占的有状态安全后端，实际密码操作交给 ESAM/厂商 SDK。
 * @note 所有方法在 Session 串行执行器内调用，必须有界且不得阻塞等待该执行器。
 * 实现须验证 MAC/签名、随机数/会话新鲜度、密文和安全策略后才返回明文；不能仅解码后放行。
 * 同一实例不得在多个 Session 中复用。硬件驱动可在外部线程预处理并提供有界结果。
 */
class DLT698_API IBackend {
   public:
    /** @brief 初始化独立安全后端；会话材料由厂商实现管理。 */
    IBackend();
    /** @brief 释放后端，实现应清除会话材料且不抛异常。 */
    virtual ~IBackend();
    /** @brief 准备客户机认证材料。
     * @return 厂商 ESAM 生成的 CONNECT 机制与材料，或 SDK/策略错误。
     */
    virtual Result<protocol::apdu::ConnectMechanism> begin_connect() = 0;
    /** @brief 校验服务器收到的认证请求并生成认证响应。
     * @param[in] request 完整 CONNECT 请求，借用仅在调用期间有效。
     * @return 标准拒绝码或经认证的 SecurityData；SDK 故障应返回错误。
     * @note 是否允许公共连接由后端安全策略决定，不能隐式退回公共认证。
     */
    virtual Result<AuthenticationResult> accept_connect(
        const protocol::apdu::ConnectRequest& request) = 0;
    /** @brief 验证客户机收到的成功 CONNECT 响应。
     * @param[in] request 本会话发出的认证请求。
     * @param[in] response 完整成功响应，包含厂商认证材料。
     * @return 认证通过或错误；失败时 Session 关闭，不能建立关联。
     */
    virtual Result<void> verify_connect(const protocol::apdu::ConnectRequest& request,
                                        const protocol::apdu::ConnectResponse& response) = 0;
    /** @brief 生成客户机外发 SECURITY-Request。
     * @param[in] application 完整内层 APDU，不得保存借用视图。
     * @return 拥有型安全封装或 SDK/策略错误。
     */
    virtual Result<protocol::apdu::SecurityRequest> protect_request(ByteView application) = 0;
    /** @brief 验证并解密服务器接收的安全请求。
     * @param[in] message 已解码的外层请求。
     * @return 完整、经认证的内层 APDU 或安全错误；失败不得向业务 provider 交付。
     */
    virtual Result<Bytes> open_request(const protocol::apdu::SecurityRequest& message) = 0;
    /** @brief 生成服务器响应/通知的 SECURITY-Response。
     * @param[in] application 完整内层 APDU，不得保存借用视图。
     * @return 拥有型安全响应或 SDK/策略错误。
     */
    virtual Result<protocol::apdu::SecurityResponse> protect_response(ByteView application) = 0;
    /** @brief 验证并解密客户机接收的安全响应/通知。
     * @param[in] message 已解码的外层响应。
     * @return 完整、经认证的内层 APDU 或错误；验证结果 DAR 不能被视为明文。
     */
    virtual Result<Bytes> open_response(const protocol::apdu::SecurityResponse& message) = 0;
    /** @brief 清除随机数、会话密钥引用和重放状态；关闭/释放/析构时调用，幂等且不抛异常。 */
    virtual void reset() noexcept = 0;
};
}  // namespace dlt698::security
