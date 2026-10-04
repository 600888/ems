/** @file plugin_handler.h
 * @brief 在主进程内将统一处理器接口适配到协议插件 C ABI。
 */
#ifndef EMS_NET_HANDLER_PLUGIN_HANDLER_H
#define EMS_NET_HANDLER_PLUGIN_HANDLER_H

#include "net/handler/protocol_handler.h"

namespace ems::net::handler {
/** @brief PIMPL 适配器；每实例串行调用，不直接链接协议栈或协议 MODULE。 */
class PluginHandler : public ProtocolHandler {
   public:
    /**
     * @brief 创建未加载的处理器。
     * @param[in,out] manager 借用的插件管理器，必须在处理器及在途调用结束后销毁。
     * @param[in] protocol_name 协议映射名称。
     * @throws std::invalid_argument 名称为空。
     */
    PluginHandler(collect::PluginManager& manager, const std::string& protocol_name);

    /** @brief 调用方结束在途调用后，停止并按实例释放库租约。 */
    ~PluginHandler() override;

    /**
     * @brief 校验并保存新的独立插件实例。
     * @param[in] config 通道或服务端配置；支持统一 role 和旧 local_role。
     * @return true 成功；false 失败，旧实例保持有效；已连接时禁止重配置。
     */
    bool initialize(const json& config) override;

    /** @brief 建立通信或启动监听。 @return true 成功；false 失败。 */
    bool connect() override;

    /** @brief 停止通信但保留配置。 @return true 成功；false 停止失败。 */
    bool disconnect() override;

    /** @brief 查询通信状态。 @return true 已连接或正在监听。 */
    bool is_connected() const override;

    /**
     * @brief 读取有序样本，最多接收 8 MiB 的 JSON 结果。
     * @param[in] point_codes 协议编码列表，最多 65536 项。
     * @param[out] results 有序样本；失败时清空。
     * @return 有效数值点数；-1 失败。
     */
    int read_batch(const std::vector<std::string>& point_codes,
                   std::vector<PointValue>& results) override;

    /**
     * @brief 写协议测点或本地出口映像。
     * @param[in] point_code 协议地址或配置别名。
     * @param[in] value 待写物理值。
     * @return true 成功；false 失败，可查询 last_error。
     */
    bool write_value(const std::string& point_code, double value) override;

    /** @brief 返回诊断副本。 @return 最近操作的错误文本。 */
    std::string last_error() const override;

    /** @brief 返回协议映射名称。 @return 构造时的协议名称。 */
    std::string protocol_name() const override;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace ems::net::handler
#endif
