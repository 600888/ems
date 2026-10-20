/** @file dlt698_protocol.h
 * @brief DL/T 698.45 TCP/串口协议接口，第三方类型不跨插件边界。
 */
#ifndef EMS_NET_PROTOCOL_DLT698_PROTOCOL_H
#define EMS_NET_PROTOCOL_DLT698_PROTOCOL_H

#include <memory>
#include <string>
#include <vector>

namespace ems::net::protocol {
/** @brief 独占客户端或只读上送服务端；调用方须串行调用各接口。 */
class Dlt698Protocol final {
   public:
    /** @brief 创建尚未配置的实例，不打开通信资源。 */
    Dlt698Protocol();
    /** @brief 关闭通信并等待全部工作线程退出。 */
    ~Dlt698Protocol();
    /** @brief 禁止复制资源所有者。
     * @param[in] other 源实例。
     */
    Dlt698Protocol(const Dlt698Protocol& other) = delete;
    /** @brief 禁止复制赋值。
     * @param[in] other 源实例。
     * @return 不可调用。
     */
    Dlt698Protocol& operator=(const Dlt698Protocol& other) = delete;
    /** @brief 校验配置并准备只读映像，失败保留原配置。
     * @param[in] json_config JSON 对象，支持 settings 对象或 JSON 字符串。
     * @return 0 成功；-1 配置错误或实例已连接。
     */
    int initialize(const std::string& json_config);
    /** @brief 建立公共关联或启动只读监听。
     * @return 0 成功；-1 通信或关联失败。
     */
    int connect();
    /** @brief 幂等关闭通信并等待线程退出，保留本地映像。
     * @return 0 成功；-1 关闭失败，诊断由 last_error 返回。
     */
    int disconnect();
    /** @brief 查询协议关联或监听状态。
     * @return 客户端已关联或服务端正在运行时为 true。
     */
    bool is_connected() const;
    /** @brief 按输入顺序读取 OAD，逐项保留 DAR、精确 Data 编码和质量。
     * @param[in] point_codes 0xOI属性索引、OI/属性/索引或配置别名。
     * @param[out] result_json JSON 数组，非数值或坏点为 value=null、quality=1。
     * @return 有效数值点数；-1 参数或连接错误。倍率由点映射层处理。
     */
    int read_batch(const std::vector<std::string>& point_codes, std::string& result_json);
    /** @brief 按已配置的精确类型写入；服务端只更新本地映像。
     * @param[in] point_code OAD 或配置别名。
     * @param[in] value 有限协议原始数值，不隐式换算倍率；整数须精确且不越界。
     * @return 0 成功；-1 未授权、类型错误、DAR 非零或通信失败；SET 不重试。
     */
    int write(const std::string& point_code, double value);
    /** @brief 获取错误信息副本。
     * @return 最近一次失败的诊断。
     */
    std::string last_error() const;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::net::protocol
#endif
