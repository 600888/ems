# 实时数据库动态库：首批实现

`ems_rtdb` 是共享库，`ems-rtdb` 是链接该库的常驻初始化入口。消费进程包含
`<realtime/realtime_client.h>` 并链接 `ems::rtdb`，不需要第三方 SDK 头文件。

本批实现记录级 read、CAS、条件删除、逐项批量及 JSON 后端配置。Boost.Interprocess
负责共享段、allocator/string/map 和命名锁，redis-plus-plus 负责 Redis 连接池、Hash
和 eval。自有代码不实现原生共享内存、socket、RESP 或跨进程分配器。

完整类型化测点/来源授权、管理 RPC、可信部署描述发布、进程接入登记和受控迁移编排
仍是后续阶段；旧采集分发器尚未切换到此客户端。当前 API 不能代替控制授权闭环。

## 构建与安装

需要 CMake 3.21+、C++17 编译器、仓库内 redis-plus-plus、系统安装的 Boost 1.83+ 头文件，
以及与工具链一致的 hiredis（作为 redis-plus-plus 的传递依赖）。Windows MSVC 在
x64 Developer PowerShell 中执行。所有命令均从仓库根目录运行。

共享内存及其 Container/Move/Intrusive 依赖全部来自同一套系统 Boost，不再从仓库
引入单独的 Interprocess 或补充组件。本机使用 `E:/boost_1_89_0`；其他系统由 CMake
查找已安装的 Boost，可通过 `BOOST_ROOT` 或 `CMAKE_PREFIX_PATH` 指定位置。
CMake 优先使用 Boost 配置包，也支持只有头文件的完整 Boost 根目录，不隐式下载：

```powershell
cmake -S backend/tests/realtime -B build/rtdb-boost-msvc -G "Ninja Multi-Config" `
  -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release `
  -DBOOST_ROOT=E:/boost_1_89_0 `
  -DCMAKE_PREFIX_PATH="$PWD/build/windows-deps-msvc/install" `
  -DCMAKE_INSTALL_PREFIX="$PWD/build/rtdb-boost-msvc/install"
cmake --build build/rtdb-boost-msvc --config Release --parallel 2
ctest --test-dir build/rtdb-boost-msvc -C Release --output-on-failure
cmake --install build/rtdb-boost-msvc --config Release --component EMSRTDB
```

将工具链和依赖前缀改为对应版本可构建 MinGW；不能混用 MSVC 导入库与 MinGW ABI。
Linux 等系统安装 Boost 开发包后可省略 `BOOST_ROOT`，使用系统搜索路径。
当前 C++ API 暴露 STL/Result，调用者必须使用兼容架构、编译器 ABI、配置与 CRT。
只编译共享内存可配置 `EMS_REALTIME_WITH_REDIS=OFF`，不强制安装 Redis 客户端依赖。

安装布局：

```text
bin/                          ems-rtdb.exe、ems_rtdb.dll 及依赖 DLL（Windows）
lib/                          导入库或 Linux SO
include/realtime/             公共接口，不包含第三方头文件
include/common/core/          Result、CallContext 等共享类型
lib/cmake/ems_rtdb/            find_package 配置与 ems::rtdb 导出目标
share/ems_rtdb/                配置示例和本文档
```

Windows 将 bin 中的全部依赖 DLL 随程序部署或加入进程 PATH；Linux 还须配置 SO
搜索路径并安装匹配的 hiredis 等运行依赖。调用方不需要第三方 SDK 头文件，仍需要运行库。

常驻入口、DLL 与业务客户端均使用同一份只读部署 JSON。配置文件本身包含后端参数，
但业务代码只传文件路径，具体解析与工厂选择封装在 DLL 内；管理 RPC 自动发现尚未接入。
配置内容应由部署管理发布，不能让各业务进程独立修改。

## 运行与调用

```powershell
# 常驻持有并初始化；Ctrl+C 停止，只关闭本进程资源，不清空共享段。
./build/rtdb-boost-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json
# 初始化/核验后退出；该选项允许创建资源，并非只读配置检查。
./build/rtdb-boost-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json --init-only
```

消费方 CMake：

```cmake
find_package(ems_rtdb 1 CONFIG REQUIRED)
target_link_libraries(my_process PRIVATE ems::rtdb)
```

消费方 C++：

```cpp
#include <realtime/realtime_client.h>

auto ctx = ems::core::make_call_context(std::chrono::seconds(2));
auto opened = ems::realtime::RealtimeClient::open(config_path, ctx);
if (!ems::core::is_ok(opened)) {
    // 按 ErrorCode 和 WriteEffect 处理失败，不根据错误文本猜后端。
    return;
}
auto client = std::get<0>(std::move(opened));
auto snapshot = client->read(point_key, ctx);
```

`open` 只挂接现有共享段；`initialize` 仅供部署保证唯一性的初始化角色调用。
`create_if_missing` 不允许写入 JSON。普通 close/析构不删除共享资源；
`SharedMemoryStore::remove` 是维护 API，调用方必须先停止并隔离全部使用者。
首批不实现管理者自动选主，唯一常驻实例由部署保证。

共享内存和 Redis 示例分别为 `backend/conf/realtime.json` 与 `realtime-redis.json`。
配置中的 type 由注册工厂识别，未编译/未知后端明确拒绝，不自动换成另一后端。
`credentials_ref` 和管理服务配置属于目标设计，本批解析器尚不支持，会明确拒绝。

## 限制与验证

- Boost 共享段使用 `ems_rtdb_boost_` 名称前缀及布局版本 2，与旧自研段不兼容。
  Windows 的 Boost 默认共享目录需要对应 OS 访问权限，沙箱可能阻止访问。
- 首批使用段级 Boost 命名锁，shard_count 只校验配置兼容。持锁进程崩溃后可持续超时；
  不能强制清锁继续访问，须隔离使用者后维护重建。普通退出不自动清段。
- Redis 目前只支持单个权威主节点。CallContext 在边界检查；第三方同步 I/O 不能硬取消，
  实际等待还受 transport_timeout_ms 与 pool_wait_timeout_ms 限制，可能超过剩余预算。
- 2026-10-24：MSVC 19.39 与 MinGW GCC 15.1 的 Release DLL/进程构建通过，
  两套工具链各有 10 项测试通过、4 项真实 Redis 用例因没有隔离服务而跳过。
  已验证 CAS、完整替换、容量/代次、普通关闭、多进程更新、死锁超时和配置权限拒绝。
- MSVC 安装包的独立消费工程仅 find_package 公共包，编译通过；常驻入口初始化后，
  两个独立消费进程均读写成功，无需 Boost/redis-plus-plus 头文件。

真实 Redis 联调设置 `EMS_TEST_REDIS_PORT` 为专用测试实例端口后运行 CTest。
测试只清理随机作用域中的已知键，不执行 FLUSHDB。当前结果不代表 Linux/ARM、
真实 Redis、控制授权或完整业务链路已验收。
