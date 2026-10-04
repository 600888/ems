#include "net/handler/modbus/modbus_proto_handler.h"

#include <stdexcept>

namespace ems::net::handler {
ModbusProtoHandler::ModbusProtoHandler(collect::PluginManager& manager, const std::string& protocol)
    : PluginHandler(manager, protocol) {
    if (protocol != "ModbusTCP" && protocol != "ModbusRTU")
        throw std::invalid_argument("Expected ModbusTCP or ModbusRTU");
}
}  // namespace ems::net::handler
