/** @file sync.hpp
 * @brief 在统一异步会话上提供同步等待适配，不创建隐藏线程。
 */
#pragma once
#include <dlt698/service/service.hpp>

namespace dlt698::service {
/** @brief 同步客户机；在事件循环回调中调用返回 busy，避免阻塞自己的响应。
 * @note 无 drive 时须由其他线程持续驱动会话运行时；超时由 Session 管理。
 * 有 drive 时由调用线程驱动，每次回调不得无限阻塞。方法不重试有副作用的操作。
 */
class SyncClientService {
   public:
    using Drive = std::function<void(std::chrono::milliseconds)>;
    /** @brief 绑定异步会话和可选驱动函数。
     * @param[in] session 非空客户机会话，应用先启动 Session。
     * @param[in] drive 空时等候外部运行线程；非空时用于 run_for 或虚拟时钟推进。
     * @throws std::invalid_argument 会话为空。
     */
    DLT698_API explicit SyncClientService(std::shared_ptr<session::Session> session,
                                          Drive drive = {});
    /** @brief 释放适配器；调用方须先结束所有同步调用。 */
    DLT698_API ~SyncClientService();
    /** @brief 禁止复制等待状态。 */
    SyncClientService(const SyncClientService&) = delete;
    /** @brief 禁止复制赋值等待状态。 */
    SyncClientService& operator=(const SyncClientService&) = delete;
    /** @brief 同步建立公共应用连接。
     * @return 响应或本地错误，远端拒绝须检查 response.result。
     */
    DLT698_API Result<protocol::apdu::ConnectResponse> connect();
    /** @brief 同步读取属性。
     * @param[in] attribute 精确 OAD。
     * @return Data/DAR 或本地错误。
     */
    DLT698_API Result<ObjectValue> get(model::Oad attribute);
    /** @brief 同步读取列表，保留部分成功。
     * @param[in] attributes 非空 OAD 列表。
     * @return 按原顺序的响应或本地错误。
     */
    DLT698_API Result<protocol::apdu::GetResponse> get_list(std::vector<model::Oad> attributes);
    /** @brief 同步查询记录，自动完成两种分段处理。
     * @param[in] record 完整记录查询。
     * @return 拥有行列内存的结果、DAR 或本地错误。
     */
    DLT698_API Result<protocol::apdu::RecordResult> get_record(protocol::apdu::GetRecord record);
    /** @brief 同步查询记录列表，保留逐项结果。
     * @param[in] records 非空查询列表。
     * @return 完整有序快照或本地错误。
     */
    DLT698_API Result<protocol::apdu::GetRecordResponse> get_record_list(
        std::vector<protocol::apdu::GetRecord> records);
    /** @brief 同步设置属性，不自动重试。
     * @param[in] attribute 精确 OAD。
     * @param[in] value 精确 Data。
     * @return 原始 DAR 或本地错误；超时后远端是否执行未知。
     */
    DLT698_API Result<std::uint8_t> set(model::Oad attribute, model::Data value);
    /** @brief 同步设置列表，不回滚部分成功。
     * @param[in] attributes 非空属性值列表。
     * @return 逐项 DAR 或本地错误。
     */
    DLT698_API Result<protocol::apdu::SetResponse> set_list(
        std::vector<protocol::apdu::SetAttribute> attributes);
    /** @brief 同步调用方法，不自动重试。
     * @param[in] method 精确 OMD。
     * @param[in] parameter 精确参数 Data。
     * @return DAR/可选 Data 或本地错误。
     */
    DLT698_API Result<ActionValue> action(model::Omd method, model::Data parameter);
    /** @brief 同步调用方法列表。
     * @param[in] methods 非空方法/参数列表。
     * @return 逐项结果或本地错误。
     */
    DLT698_API Result<protocol::apdu::ActionResponse> action_list(
        std::vector<protocol::apdu::ActionMethod> methods);
    /** @brief 同步释放应用连接，保留物理通道。
     * @return 释放成功或本地错误。
     */
    DLT698_API Result<void> release();

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace dlt698::service
