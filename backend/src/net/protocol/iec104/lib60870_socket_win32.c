/* lib60870 的 Windows HAL 默认 SO_REUSEADDR 允许其他监听者抢占同一地址。
 * 仅在本包编译时将该选项替换为独占绑定，第三方源码保持原样。 */
#include <winsock2.h>
#include <ws2tcpip.h>

enum { EMS_EXCLUSIVE_ADDRESS_USE = SO_EXCLUSIVEADDRUSE };

#undef SO_REUSEADDR
#define SO_REUSEADDR EMS_EXCLUSIVE_ADDRESS_USE

#include "hal/socket/win32/socket_win32.c"
