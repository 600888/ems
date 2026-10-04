/** @file iec104_handler.h
 * @brief IEC104 客户端与只读上送处理器。
 */
#ifndef EMS_NET_HANDLER_IEC104_H
#define EMS_NET_HANDLER_IEC104_H

#include "net/handler/plugin_handler.h"

namespace ems::net::handler {
/** @brief 使用现有 IEC104 动态插件的处理器，role 决定客户端或服务端。 */
class Iec104Handler final : public PluginHandler {
   public:
    /**
     * @brief 创建未加载的协议处理器。
     * @param[in,out] manager 借用的管理器，须比处理器及其在途调用存活更久。
     */
    explicit Iec104Handler(collect::PluginManager& manager);
};
}  // namespace ems::net::handler
#endif
