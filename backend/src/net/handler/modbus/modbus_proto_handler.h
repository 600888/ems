/** @file modbus_proto_handler.h
 * @brief MODBUS 客户端与只读上送处理器。
 */
#ifndef EMS_NET_HANDLER_MODBUS_H
#define EMS_NET_HANDLER_MODBUS_H

#include "net/handler/plugin_handler.h"

namespace ems::net::handler {
/** @brief 使用现有 MODBUS 动态插件的处理器，role 决定客户端或服务端。 */
class ModbusProtoHandler final : public PluginHandler {
   public:
    /**
     * @brief 创建未加载的协议处理器。
     * @param[in,out] manager 借用的管理器，须比处理器及其在途调用存活更久。
     * @param[in] protocol ModbusTCP 或 ModbusRTU。
     */
    explicit ModbusProtoHandler(collect::PluginManager& manager,
                                const std::string& protocol = "ModbusTCP");
};
}  // namespace ems::net::handler
#endif
