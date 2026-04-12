/**
 * @file modbus_handler.cpp
 * @brief Modbus 协议处理器实现
 * @author cdy
 * @date 2026-04-08
 */

#include "modbus_handler.h"

namespace ems {
namespace net {
namespace device {

template class ModbusServerHandler<void>;
template class ModbusClientHandler<void>;

} // namespace device
} // namespace net
} // namespace ems
