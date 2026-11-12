#include <dlt698/security/backend.hpp>

namespace dlt698::security {
// 在核心共享库内定义基类构造/析构，保证安装包中的厂商派生类能链接并跨 DLL 析构。
IBackend::IBackend() = default;
IBackend::~IBackend() = default;
}  // namespace dlt698::security
