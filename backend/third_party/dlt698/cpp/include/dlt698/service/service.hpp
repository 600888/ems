/** @file service.hpp
 * @brief 基于会话事务的异步读写/方法客户机及对象分发服务器。
 */
#pragma once
#include <dlt698/service/object.hpp>
#include <dlt698/session/session.hpp>

namespace dlt698::service {
class ClientService {
   public:
    /** @brief 绑定客户机会话。
     * @param[in] session 非空会话，连接和生命周期由应用管理。
     * @throws std::invalid_argument 会话为空。
     */
    DLT698_API explicit ClientService(std::shared_ptr<session::Session> session);
    /** @brief 读取一个完整属性或索引元素。
     * @param[in] attribute 精确 OAD。
     * @param[in] handler 返回单个 Data/DAR 或本地事务错误。
     */
    DLT698_API void async_get(model::Oad attribute,
                              std::function<void(Result<ObjectValue>)> handler);
    /** @brief 批量读取属性，保留逐项结果和原顺序。
     * @param[in] attributes 非空 OAD 列表。
     * @param[in] handler 返回 GET NormalList 响应或错误。
     */
    DLT698_API void async_get_list(std::vector<model::Oad> attributes,
                                   session::Session::GetHandler handler);
    /** @brief 查询一项记录，自动收齐分块。
     * @param[in] record 完整行列查询。
     * @param[in] handler 返回精确记录结果或事务错误。
     */
    DLT698_API void async_get_record(
        protocol::apdu::GetRecord record,
        std::function<void(Result<protocol::apdu::RecordResult>)> handler);
    /** @brief 查询记录列表，保持逐项 DAR 和原顺序。
     * @param[in] records 非空记录列表。
     * @param[in] handler 返回完整快照或错误。
     */
    DLT698_API void async_get_record_list(std::vector<protocol::apdu::GetRecord> records,
                                          session::Session::RecordHandler handler);
    /** @brief 设置单个属性。
     * @param[in] attribute 精确 OAD。
     * @param[in] value 精确 Data。
     * @param[in] handler 返回原始 DAR 或本地错误，不自动重试。
     */
    DLT698_API void async_set(model::Oad attribute, model::Data value,
                              std::function<void(Result<std::uint8_t>)> handler);
    /** @brief 批量设置，逐项执行，部分成功不回滚。
     * @param[in] attributes 非空属性值列表。
     * @param[in] handler 返回按原顺序的 DAR 或本地错误。
     */
    DLT698_API void async_set_list(std::vector<protocol::apdu::SetAttribute> attributes,
                                   session::Session::SetHandler handler);
    /** @brief 调用单个方法。
     * @param[in] method 精确 OMD。
     * @param[in] parameter 精确 Data 参数。
     * @param[in] handler 返回 DAR/可选 Data 或本地错误，不自动重试。
     */
    DLT698_API void async_action(model::Omd method, model::Data parameter,
                                 std::function<void(Result<ActionValue>)> handler);
    /** @brief 顺序调用多个方法，保留每项结果。
     * @param[in] methods 非空 OMD/参数列表。
     * @param[in] handler 返回列表响应或本地错误。
     */
    DLT698_API void async_action_list(std::vector<protocol::apdu::ActionMethod> methods,
                                      session::Session::ActionHandler handler);

   private:
    std::shared_ptr<session::Session> session_;
};

class ServerService {
   public:
    /** @brief 将对象目录接入服务器会话的 GET 分发。
     * @param[in] session 非空服务器会话。
     * @param[in] objects 非空对象目录，处理器持有目录，不捕获本对象地址。
     * @throws std::invalid_argument 参数为空。
     */
    DLT698_API ServerService(std::shared_ptr<session::Session> session,
                             std::shared_ptr<ObjectRegistry> objects);

   private:
    std::shared_ptr<session::Session> session_;
    std::shared_ptr<ObjectRegistry> objects_;
};
}  // namespace dlt698::service
