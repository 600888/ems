/** @file dlt645_handler.h
 * @brief DLT645 客户端与只读上送处理器。
 */
#ifndef EMS_NET_HANDLER_DLT645_H
#define EMS_NET_HANDLER_DLT645_H

#include "net/handler/plugin_handler.h"

namespace ems::net::handler {
/** @brief 使用现有 DLT645 动态插件的处理器，role 决定客户端或服务端。 */
class Dlt645Handler final : public PluginHandler {
   public:
    /**
     * @brief 创建未加载的协议处理器。
     * @param[in,out] manager 借用的管理器，须比处理器及其在途调用存活更久。
     */
    explicit Dlt645Handler(collect::PluginManager& manager);
};
}  // namespace ems::net::handler
#endif
