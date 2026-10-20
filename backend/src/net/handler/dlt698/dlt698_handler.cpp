#include "net/handler/dlt698/dlt698_handler.h"

#include <stdexcept>

namespace ems::net::handler {
Dlt698Handler::Dlt698Handler(collect::PluginManager& manager) : PluginHandler(manager, "DLT698") {}
}  // namespace ems::net::handler
