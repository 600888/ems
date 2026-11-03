/** @file iec61850_protocol.h
 * @brief IEC 61850 MMS 轮询/报告客户端与只读服务端，原生对象和线程隐藏于 Impl。
 */
#ifndef EMS_NET_PROTOCOL_IEC61850_PROTOCOL_H
#define EMS_NET_PROTOCOL_IEC61850_PROTOCOL_H

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {
/** @brief 独占客户端 association 或固定模型服务器；公开调用串行，不跨动态库传递本对象。 */
class Iec61850Protocol final {
   public:
    /** @brief 创建未配置的实例，不打开通信资源。 */
    Iec61850Protocol();
    /** @brief 关闭 association 并释放所有原生资源，等待栈线程退出。 */
    ~Iec61850Protocol();
    /** @brief 禁止复制会话所有者。
     * @param[in] other 源实例。
     */
    Iec61850Protocol(const Iec61850Protocol& other) = delete;
    /** @brief 禁止复制赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    Iec61850Protocol& operator=(const Iec61850Protocol& other) = delete;
    /** @brief 校验配置及点白名单，不访问网络；失败保留原配置。
     * @param[in] json_config JSON 对象，支持 settings 对象或对象字符串。
     * @return 0 成功；-1 配置非法、能力不支持或实例仍连接。
     */
    int initialize(const std::string& json_config);
    /** @brief 建立并校验客户端会话及报告订阅，或启动固定模型监听。
     * @return 0 成功；-1 建连、模型、必需报告或监听失败，部分原生资源已清理。
     */
    int connect();
    /** @brief 停止报告任务、回调和会话；服务端保留本地映像以供重新监听。
     * @return 0 成功；-1 清理失败，调用方必须保留实例与动态库。
     */
    int disconnect();
    /** @brief 查询 association 或实际监听状态，不代表各点已有有效值。
     * @return 客户端已关联或服务端正在监听时为 true。
     */
    bool is_connected() const;
    /** @brief 按白名单批读：poll 访问 MMS，report/hybrid 读取缓存，server 读取本地映像。
     * @param[in] point_codes 初始化目录中的 code，允许重复，结果保留请求顺序。
     * @param[out] result_json 有界 JSON 数组，逐点错误为 value=null、quality=1。
     * @return quality=0 的点数；-1 参数非法、序列化超限或实例未初始化。
     */
    int read_batch(const std::vector<std::string>& point_codes, std::string& result_json);
    /** @brief 服务端本地更新值/q/t；客户端写入仍拒绝，不发送远端控制。
     * @param[in] point_code 已配置的值点或显式 quality_code。
     * @param[in] value 有限原始值；质量别名接受 IEC q 位掩码。
     * @return 0 本地更新成功；-1 客户端写入、类型或别名错误，原映像不变。
     */
    int write(const std::string& point_code, double value);
    /** @brief 服务端原子更新整批出口映像，客户端拒绝此操作。
     * @param[in] samples_json 带 code/value/iec_quality/timestamp 的 JSON 数组。
     * @return 0 成功；-1 参数错误、不支持或未初始化，原映像不变。
     */
    int update_image(const std::string& samples_json);
    /** @brief 返回最近一次操作错误的副本。
     * @return 错误说明；成功操作后可为空。
     */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::net::protocol
#endif
