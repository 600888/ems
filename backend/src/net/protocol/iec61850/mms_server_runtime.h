#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {
/** @brief 插件内部的固定模型只读服务器，所有协议对象由本对象独占。 */
class MmsReadOnlyServer final {
   public:
    /** @brief 校验点表并构建固定模型，不监听网络；错误以异常返回。
     * @param[in] options_json iec61850 配置对象的 JSON 文本。
     * @param[in] host 监听 IPv4 地址。
     * @param[in] port 监听端口。
     */
    MmsReadOnlyServer(const std::string& options_json, const std::string& host, int port);
    /** @brief 停止所有会话和回调，再销毁服务器及模型。 */
    ~MmsReadOnlyServer();
    /** @brief 启动监听，重复调用幂等；失败清理部分监听资源。
     * @return 0 成功；-1 失败，原因由 last_error 返回。
     */
    int connect();
    /** @brief 停止监听和所有会话，保留本地映像供再次启动。
     * @return 0 成功；-1 停止失败。
     */
    int disconnect();
    /** @brief 查询实际 MMS 监听状态。
     * @return 正在监听时为 true。
     */
    bool is_connected() const;
    /** @brief 从同一模型临界区读取本地映像，保留输入顺序。
     * @param[in] codes 已配置的值点或质量更新别名。
     * @param[out] output JSON 结果数组。
     * @return 有效点数；-1 参数错误或结果超限。
     */
    int read_batch(const std::vector<std::string>& codes, std::string& output);
    /** @brief 本地更新值及其 q/t，或通过显式 quality_code 标记输入失效。
     * @param[in] code 值点编码或 quality_code；不接受任意对象引用。
     * @param[in] value 有限且符合模型的原始数值，质量别名为 IEC q 位掩码。
     * @return 0 成功；-1 参数、类型或权限错误，原映像保持不变。
     */
    int write(const std::string& code, double value);
    /** @brief 全批校验后原子提交本地出口映像，保留来源 UTC 毫秒时标。
     * @param[in] samples_json 含 code/value/iec_quality/timestamp 的数组；null 值仅更新 q/t。
     * @return 0 成功；-1 参数错误，整批不提交。
     */
    int update_image(const std::string& samples_json);
    /** @brief 返回最近错误的副本。
     * @return 诊断文本。
     */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::net::protocol
