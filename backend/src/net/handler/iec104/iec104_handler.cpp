#include "net/handler/iec104/iec104_handler.h"

#include <stdexcept>

namespace ems::net::handler {
Iec104Handler::Iec104Handler(collect::PluginManager& manager) : PluginHandler(manager, "IEC104") {}
}  // namespace ems::net::handler
