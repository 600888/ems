#include "net/handler/dlt645/dlt645_handler.h"

#include <stdexcept>

namespace ems::net::handler {
Dlt645Handler::Dlt645Handler(collect::PluginManager& manager) : PluginHandler(manager, "DLT645") {}
}  // namespace ems::net::handler
