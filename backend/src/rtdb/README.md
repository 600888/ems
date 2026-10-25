# 统一实时数据库模块 rtdb

`ems_rtdb` 是共享库，`ems-rtdb` 是链接该库的常驻初始化入口。消费进程包含
`<rtdb/client/realtime_client.h>` 并链接 `ems::rtdb`，不需要第三方 SDK 头文件。

本批实现记录级 read、CAS、条件删除、逐项批量及 JSON 后端配置。Boost.Interprocess
负责共享段、allocator/string/map 和命名锁，redis-plus-plus 负责 Redis 连接池、Hash
和 eval。自有代码不实现原生共享内存、socket、RESP 或跨进程分配器。

旧 Redis 专用实现已移除。RtDb、RtDbManager、RTDBEMS 的四类测点、设备快照、拓扑和
配置缓存编入同一动态库，底层只调用 RealtimeClient。采集分发器已接入统一接口。

类型化门面 `ems::rtdb::RealtimeService`（`rtdb/client/realtime_service.h`）在同一动态库内
提供测点/设备/通道状态、来源授权核对、序号与重复判断、新鲜度、作用域配置版本记录和
能力运行条件。运行目录由 `buildRuntimeCatalog()` 从配置服务投影的设备树快照编译
（只有设备树，没有资产概念）。管理 RPC、来源授权
协调流程、可信部署发现和受控迁移编排仍待实现，业务锁不能代替控制授权。

```text
backend/include/rtdb/         公共头，命名空间 ems::rtdb
  client/                    记录客户端、测点/设备/拓扑业务门面
  model/                     业务配置、类型别名和本进程配置缓存
  store/                     记录契约和后端工厂
  backend/                   后端打开与维护接口
backend/src/rtdb/
  client/                    业务序列化与有界 CAS 更新
  model/                     配置缓存
  store/                     记录校验、编码与版本工具
  backend/                   系统 Boost.Interprocess / redis-plus-plus
  process/                   ems-rtdb 常驻入口
backend/tests/rtdb/           后端、业务、采集分发器、安装包消费测试
```

`rtdb` 构建名称仅为 `ems_rtdb` 的别名，不再生成第二个静态数据库库。
原 realtime 公共头路径和命名空间已统一为 rtdb，调用方须重新编译。

## 构建与安装

需要 CMake 3.21+、C++17 编译器、仓库内 redis-plus-plus、系统安装的 Boost 1.83+ 头文件，
以及与工具链一致的 hiredis（作为 redis-plus-plus 的传递依赖）。Windows MSVC 在
x64 Developer PowerShell 中执行。所有命令均从仓库根目录运行。

共享内存及其 Container/Move/Intrusive 依赖全部来自同一套系统 Boost，不再从仓库
引入单独的 Interprocess 或补充组件。本机使用 `E:/boost_1_89_0`；其他系统由 CMake
查找已安装的 Boost，可通过 `BOOST_ROOT` 或 `CMAKE_PREFIX_PATH` 指定位置。
CMake 优先使用 Boost 配置包，也支持只有头文件的完整 Boost 根目录，不隐式下载：

```powershell
cmake -S backend/tests/rtdb -B build/rtdb-unified-msvc -G "Ninja Multi-Config" `
  -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release `
  -DBOOST_ROOT=E:/boost_1_89_0 `
  -DCMAKE_PREFIX_PATH="$PWD/build/windows-deps-msvc/install" `
  -DCMAKE_INSTALL_PREFIX="$PWD/build/rtdb-unified-msvc/install"
cmake --build build/rtdb-unified-msvc --config Release --parallel 2
ctest --test-dir build/rtdb-unified-msvc -C Release --output-on-failure
cmake --install build/rtdb-unified-msvc --config Release --component EMSRTDB
```

将工具链和依赖前缀改为对应版本可构建 MinGW；不能混用 MSVC 导入库与 MinGW ABI。
Linux 等系统安装 Boost 开发包后可省略 `BOOST_ROOT`，使用系统搜索路径。
当前 C++ API 暴露 STL/Result，调用者必须使用兼容架构、编译器 ABI、配置与 CRT。
只编译共享内存可配置 `EMS_REALTIME_WITH_REDIS=OFF`，不强制安装 Redis 客户端依赖。

安装布局：

```text
bin/                          ems-rtdb 可执行程序及 Windows 运行依赖 DLL
lib/                          导入库或 Linux SO
include/rtdb/                 公共接口，Boost/Redis 实现头不外露
include/common/core/          Result、CallContext 等共享类型
lib/cmake/ems_rtdb/            find_package 配置与 ems::rtdb 导出目标
share/ems_rtdb/                配置示例和本文档
```

Windows 将 bin 中的程序与依赖 DLL 一起部署；Linux 还须配置 SO
搜索路径并安装匹配的 hiredis 等运行依赖。调用方无需安装 Boost/Redis SDK，仍需要运行库。
原有业务模型使用的 JSON 头随 SDK 安装，无需调用方单独配置依赖。

常驻入口、DLL 与业务客户端均使用同一份只读部署 JSON。配置文件本身包含后端参数，
但业务代码只传文件路径，具体解析与工厂选择封装在 DLL 内；管理 RPC 自动发现尚未接入。
配置内容应由部署管理发布，不能让各业务进程独立修改。

## 运行与调用

```powershell
# 常驻持有并初始化；Ctrl+C 停止，只关闭本进程资源，不清空共享段。
./build/rtdb-unified-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json
# 初始化/核验后退出；该选项允许创建资源，并非只读配置检查。
./build/rtdb-unified-msvc/install/bin/ems-rtdb.exe --config backend/conf/realtime.json --init-only
```

消费方 CMake：

```cmake
find_package(ems_rtdb 1 CONFIG REQUIRED)
target_link_libraries(my_process PRIVATE ems::rtdb)
```

消费方 C++：

```cpp
#include <rtdb/client/realtime_client.h>

auto ctx = ems::core::make_call_context(std::chrono::seconds(2));
auto opened = ems::rtdb::RealtimeClient::open(config_path, ctx);
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
- 验证环境为 MSVC 19.39、MinGW GCC 15.1 和系统 Boost 1.89。
  后端、业务门面、配置缓存导入、采集分发器及安装包消费均纳入测试。
  两套工具链各通过 45 项、跳过 4 项真实 Redis 用例；跳过不视为通过。
  MSVC 后端主工程的 net_collect 及四个 RTDB 示例构建通过；安装包独立工程
  仅 find_package 公共包，两个独立进程的记录 API 和业务 API 调用均成功。

真实 Redis 联调设置 `EMS_TEST_REDIS_PORT` 为专用测试实例端口后运行 CTest。
测试只清理随机作用域中的已知键，不执行 FLUSHDB。当前结果不代表 Linux/ARM、
真实 Redis、控制授权或完整业务链路已验收。

## 业务迁移约定

```cpp
#include <rtdb/client/rtdb.h>
ems::rtdb::RtDbConfig config;
config.config_path = "conf/realtime.json";
config.scope_code = "station001";
auto client = ems::rtdb::createRtDb(config);
```

业务配置只指定配置文件、作用域、逻辑命名空间 prefix 和调用预算。四类测点保留完整
模型字段，以单记录 CAS 替换；批量逐项提交，有失败返回 false，已成功项不回滚。
旧 bool 门面通过 getLastError 提供说明；需要结构化错误/WriteEffect 时使用记录 API。
本进程配置缓存沿用原模型，不是跨进程配置发布服务。

旧逻辑字符串、文档和测点包装存放在作用域内的 business 设备路径下，逻辑键以可逆
十六进制路径编码，prefix+逻辑键总长最多 217 字节。这是兼容映射，不是来源授权门面。
旧 Redis 原始键不会自动读入新布局，部署时须停写并显式迁移或重新采集，不能混跑旧写者。
setHash 保留合并语义，replaceHash 和测点写入完整替换。文档 TTL 使用 expires_ms，
访问时检查并惰性清理，不做后台回收；参与者依赖一致的系统时间。容量满时明确失败。
keys 只支持 * 和 ?，按作用域枚举并筛选命名空间；不保证整体快照，错误不返回截断成功。

EngineConfig/DataDispatcherConfig 使用 rtdbConfigPath_、rtdbScopeCode_ 接入统一存储。
原 Redis 地址字段仅用于通知；通知可选 Redis、ZeroMQ 或 None，与状态后端独立。
存储接口不再负责 publish/subscribe。共享内存缺失时业务客户端报错，不自行创建资源。

RTDBEMS 的旧命令队列和历史辅助接口不代表可靠投递或历史数据库；正式控制与历史链路
仍由独立协调、消息和持久存储组件承担。业务锁使用 CAS 获取和完整 owner 比较释放，
不提供来源 fencing 或控制授权。
