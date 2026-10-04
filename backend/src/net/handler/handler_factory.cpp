#include "net/handler/dlt645/dlt645_handler.h"
#include "net/handler/iec104/iec104_handler.h"
#include "net/handler/modbus/modbus_proto_handler.h"
#include "net/handler/protocol_handler.h"

namespace ems::net::handler {
std::unique_ptr<ProtocolHandler> create_handler(collect::PluginManager& manager,
                                                const std::string& protocol) {
    if (protocol == "ModbusTCP" || protocol == "ModbusRTU")
        return std::make_unique<ModbusProtoHandler>(manager, protocol);
    if (protocol == "IEC104") return std::make_unique<Iec104Handler>(manager);
    if (protocol == "DLT645") return std::make_unique<Dlt645Handler>(manager);
    return std::make_unique<PluginHandler>(manager, protocol);
}
}  // namespace ems::net::handler
